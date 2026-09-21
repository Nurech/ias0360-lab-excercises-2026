/* imu to sd. s start, x stop, i info, l list, d dump. */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/cyw43_arch.h"

#include "icm20948.h"

#include "ff.h"
#include "f_util.h"
#include "sd_card.h"
#include "hw_config.h"

#ifndef DAQ_USE_CORE1
#define DAQ_USE_CORE1     1
#endif
#ifndef DAQ_BINARY
#define DAQ_BINARY        1    /* 0 = csv if you want to read it by eye */
#endif
#ifndef DAQ_BLOCK_SAMPLES
#define DAQ_BLOCK_SAMPLES 64   /* one write = two sd sectors, that's the 1000 hz trick */
#endif
#ifndef DAQ_SYNC_BLOCKS
#define DAQ_SYNC_BLOCKS   16
#endif
#ifndef DAQ_TARGET_HZ
#define DAQ_TARGET_HZ     1000
#endif
#ifndef DAQ_RUN_SECONDS
#define DAQ_RUN_SECONDS   10
#endif
#ifndef DAQ_QUEUE_LEN
#define DAQ_QUEUE_LEN     512
#endif

typedef struct __attribute__((packed)) {
    uint32_t t_us;
    int16_t ax, ay, az, gx, gy, gz;
} daq_sample_t;

_Static_assert(sizeof(daq_sample_t) == 16, "record must stay 16 bytes");

#define DAQ_CSV_HEADER "t_us,ax,ay,az,gx,gy,gz\n"

#define DAQ_PERIOD_US   (1000000 / DAQ_TARGET_HZ)

#if DAQ_BINARY
#  define DAQ_EXT         "bin"
#  define DAQ_RECORD_MAX  16
#else
#  define DAQ_EXT         "csv"
#  define DAQ_RECORD_MAX  64
#endif

#define DAQ_BLOCK_BYTES (DAQ_BLOCK_SAMPLES * DAQ_RECORD_MAX)
#define PATH_MAX_LEN    64

static FATFS       g_fs;
static sd_card_t  *g_sd    = NULL;
static const char *g_drive = NULL;
static FIL         g_file;

static char     g_path[PATH_MAX_LEN];
static uint8_t  g_block[DAQ_BLOCK_BYTES];
static uint32_t g_block_fill;
static uint32_t g_blocks_since_sync;

static volatile bool     g_running = false;
static volatile uint32_t g_dropped = 0;

static uint32_t g_last_samples = 0;
static uint32_t g_last_dropped = 0;
static float    g_last_seconds = 0.0f;
static float    g_last_rate    = 0.0f;

static bool g_boot_fault = false;
static bool g_run_failed = false;

static bool fault_active(void) { return g_boot_fault || g_run_failed; }

#if DAQ_USE_CORE1
static queue_t g_queue;
#endif

typedef enum { LED_IDLE, LED_CAPTURE, LED_ERROR } led_state_t;

static void led_put(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
}

static void led_tick(led_state_t state) {
    static bool on = false;
    static absolute_time_t next = { 0 };

    if (state == LED_CAPTURE) { if (!on) { on = true; led_put(true); } return; }

    if (!time_reached(next)) return;
    on = !on;
    printf("LT put %d\n", (int)on);
    led_put(on);
    printf("LT ok\n");
    next = make_timeout_time_ms(state == LED_ERROR ? 100 : 500);
}

/* raw reads, not the Fast ones. those average 8 samples and look stuck. */
static void sample_read(daq_sample_t *s) {
    int16_t ax, ay, az, gx, gy, gz;

    icm20948AccelRawRead(&ax, &ay, &az);
    icm20948GyroRawRead(&gx, &gy, &gz);

    s->t_us = time_us_32();
    s->ax = ax; s->ay = ay; s->az = az;
    s->gx = gx; s->gy = gy; s->gz = gz;
}

/* wait until the next tick, don't sleep(period) after the work or the rate drifts. */
static absolute_time_t  g_next;
static volatile uint32_t g_late = 0;

static void pace_init(void) {
    g_next = get_absolute_time();
    g_late = 0;
}

static void pace_wait(void) {
    g_next = delayed_by_us(g_next, DAQ_PERIOD_US);
    if (absolute_time_diff_us(get_absolute_time(), g_next) < 0) g_late++;

#if DAQ_USE_CORE1
    busy_wait_until(g_next);   /* sleep_until() would irq core0 */
#else
    sleep_until(g_next);
#endif
}

/* ==========================================================================
 * Storage sink
 * ========================================================================== */

/* Encode one record at dst. At least DAQ_RECORD_MAX bytes are available there.
 * Returns the number of bytes written. */
static uint32_t record_encode(uint8_t *dst, const daq_sample_t *s) {
#if DAQ_BINARY
    memcpy(dst, s, sizeof(*s));
    return (uint32_t)sizeof(*s);
#else
    int n = snprintf((char *)dst, DAQ_RECORD_MAX, "%lu,%d,%d,%d,%d,%d,%d\n",
                     (unsigned long)s->t_us,
                     s->ax, s->ay, s->az, s->gx, s->gy, s->gz);
    return (n > 0) ? (uint32_t)n : 0u;
#endif
}

static FRESULT block_write_out(void) {
    if (g_block_fill == 0) return FR_OK;

    UINT bw = 0;
    FRESULT fr = f_write(&g_file, g_block, (UINT)g_block_fill, &bw);
    if (fr == FR_OK && bw != g_block_fill) fr = FR_DISK_ERR;   /* card full, or card gone */

    /* Say which FatFs error it was, here, where it happened. A capture that dies
     * silently is indistinguishable from a capture that simply finished early. */
    if (fr != FR_OK) {
        printf("f_write(%lu B) -> %s (%d) wrote=%u\n",
               (unsigned long)g_block_fill, FRESULT_str(fr), fr, (unsigned)bw);
        g_block_fill = 0;
        return fr;
    }
    g_block_fill = 0;

    if (++g_blocks_since_sync >= DAQ_SYNC_BLOCKS) {
        g_blocks_since_sync = 0;
        fr = f_sync(&g_file);
        if (fr != FR_OK) printf("f_sync -> %s (%d)\n", FRESULT_str(fr), fr);
    }
    return fr;
}

/* Append one record to the staging block and hand the block to the card only when
 * it can no longer hold a worst-case record.
 *
 * An SD card writes a whole 512-byte sector however few bytes it is given, so a
 * 16-byte write wastes 97% of the transfer, and an f_sync() on top of it rewrites
 * the FAT and the directory entry as well. One f_write per DAQ_BLOCK_SAMPLES samples
 * is what turns 86 Hz into 1000 Hz; block_write_out() already does that single
 * f_write and the every-DAQ_SYNC_BLOCKS f_sync.
 *
 * The room test is in terms of DAQ_RECORD_MAX rather than the length just encoded.
 * That is what keeps the CSV path from running off the end of g_block when one line
 * happens to be longer than the previous ones. */
static FRESULT sink_push(const daq_sample_t *s) {
    g_block_fill += record_encode(g_block + g_block_fill, s);

    if (g_block_fill + DAQ_RECORD_MAX > DAQ_BLOCK_BYTES) return block_write_out();
    return FR_OK;                       /* sink_flush() pushes the partial block at the end */
}

static FRESULT sink_flush(void) {
    FRESULT fr = block_write_out();
    if (fr == FR_OK) fr = f_sync(&g_file);   /* capture_run() reports this one */
    return fr;
}

/* Push one sample and report the FRESULT if the card refused it. Returns false when
 * the capture has to stop. */
static bool sink_push_ok(const daq_sample_t *s, uint32_t samples) {
    FRESULT fr = sink_push(s);
    if (fr == FR_OK) return true;
    printf("sink_push -> %s (%d) after %lu samples\n",
           FRESULT_str(fr), fr, (unsigned long)samples);
    return false;
}

static void sink_reset(void) {
    g_block_fill = 0;
    g_blocks_since_sync = 0;
}

/* ==========================================================================
 * SD card
 * ========================================================================== */

static bool sd_init_and_mount(void) {
    if (!sd_init_driver()) { printf("sd_init_driver() failed\n"); return false; }

    g_sd = sd_get_by_num(0);
    if (!g_sd) { printf("sd_get_by_num(0) == NULL\n"); return false; }

    g_drive = sd_get_drive_prefix(g_sd);
    if (!g_drive) { printf("sd_get_drive_prefix() == NULL\n"); return false; }

    FRESULT fr = f_mount(&g_fs, g_drive, 1);
    printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);

    if (fr == FR_NO_FILESYSTEM) {
        static BYTE work[4096];                 /* >= FF_MAX_SS; static, far too big for the stack */
        MKFS_PARM opt = { FM_FAT | FM_SFD, 0, 0, 0, 0 };
        fr = f_mkfs(g_drive, &opt, work, sizeof work);
        printf("f_mkfs -> %s (%d)\n", FRESULT_str(fr), fr);
        if (fr == FR_OK) {
            fr = f_mount(&g_fs, g_drive, 1);
            printf("f_mount(after mkfs) -> %s (%d)\n", FRESULT_str(fr), fr);
        }
    }

    if (fr != FR_OK) { printf("mount failed: %s (%d)\n", FRESULT_str(fr), fr); return false; }
    return true;
}

/* Full hardware re-probe: the IMU first, then drop the stale FatFs volume and mount
 * the card again.
 *
 * This is what makes the examiner demo work - pull the card mid-capture, put it back,
 * press 's'. f_mount(NULL, drive, 0) unregisters the dead volume, so the next mount
 * really re-initialises the card instead of writing through a handle that describes a
 * card which is no longer there. sd_init_driver() is idempotent, so calling it again
 * is free. */
static bool hardware_init(void) {
    IMU_EN_SENSOR_TYPE type = IMU_EN_SENSOR_TYPE_NULL;
    imuInit(&type);
    bool imu_ok = (type == IMU_EN_SENSOR_TYPE_ICM20948);
    printf("imu: %s\n", imu_ok ? "ICM-20948 on i2c1 GP6/GP7" : "NOT FOUND (check the board)");

    if (g_drive) f_mount(NULL, g_drive, 0);
    g_drive = NULL;

    bool sd_ok = sd_init_and_mount();
    if (sd_ok) printf("sd: mounted on %s (SDIO CMD=GP18 D0=GP19)\n", g_drive);
    else       printf("sd: unavailable - captures will fail\n");

    return imu_ok && sd_ok;
}

/* 0:/daq_000.<ext>, first index that does not exist yet, so runs never overwrite. */
static bool next_free_path(void) {
    for (unsigned idx = 0; idx < 1000; idx++) {
        snprintf(g_path, sizeof g_path, "%s/daq_%03u." DAQ_EXT, g_drive, idx);
        FILINFO fno;
        if (f_stat(g_path, &fno) == FR_NO_FILE) return true;
    }
    printf("no free daq_NNN." DAQ_EXT " slot left on the card\n");
    return false;
}

static void list_files(void) {
    DIR dir;
    FILINFO fno;
    FRESULT fr = f_opendir(&dir, g_drive);
    if (fr != FR_OK) { printf("f_opendir -> %s (%d)\n", FRESULT_str(fr), fr); return; }

    uint32_t files = 0;
    uint64_t bytes = 0;
    printf("--- %s ---\n", g_drive);
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') break;
        if (fno.fattrib & AM_DIR) { printf("[DIR ] %s\n", fno.fname); continue; }
        files++;
        bytes += (uint64_t)fno.fsize;
        printf("[FILE] %-16s %10lu bytes\n", fno.fname, (unsigned long)fno.fsize);
    }
    f_closedir(&dir);
    printf("%lu file(s), %llu bytes total\n",
           (unsigned long)files, (unsigned long long)bytes);
}

/* Stream the last capture back over serial as hex, so the host can check the data
 * is real rather than trusting the rate the firmware reports. A file of zeros
 * would satisfy every other check here; this is the one that catches it. */
static void dump_file(void) {
    if (!g_drive || g_path[0] == '\0') {
        printf("nothing captured yet - press s first\n");
        return;
    }

    FIL f;
    FRESULT fr = f_open(&f, g_path, FA_READ);
    if (fr != FR_OK) {
        printf("f_open(%s) -> %s (%d)\n", g_path, FRESULT_str(fr), fr);
        return;
    }

    FSIZE_t total = f_size(&f);
    printf("DUMP begin file=%s bytes=%lu record=%u\n",
           g_path, (unsigned long)total, (unsigned)sizeof(daq_sample_t));

    static uint8_t buf[512];
    UINT got = 0;
    unsigned long sent = 0;
    while (f_read(&f, buf, sizeof buf, &got) == FR_OK && got > 0) {
        for (UINT i = 0; i < got; i++) {
            printf("%02x", buf[i]);
            /* 64 bytes per line keeps each printf short and the output readable */
            if ((sent + i + 1) % 64 == 0) printf("\n");
        }
        sent += got;
    }
    if (sent % 64) printf("\n");
    f_close(&f);
    printf("DUMP end bytes=%lu\n", sent);
}

/* ==========================================================================
 * Producer (core1 configuration only)
 * ========================================================================== */

#if DAQ_USE_CORE1
/* The producer. Reads the IMU, timestamps it AT SAMPLE TIME, and hands it over
 * without ever blocking.
 *
 * queue_try_add is the whole point. queue_add_blocking would couple the two cores
 * straight back together: the moment core0 stalls inside f_write() waiting on the
 * card, core1 would stop sampling too and the 1000 Hz schedule would quietly become
 * whatever the card felt like - and nothing would record that it happened, the file
 * would just have a gap in its timestamps. With the try-variant a full queue costs
 * exactly one sample and that sample is counted, so the DONE line can state how many
 * samples the storage side could not absorb. dropped = 0 at or above 500 Hz is a
 * passing run; dropped > 0 means the consumer is the bottleneck, so the write path
 * or the record format is what to fix, not DAQ_TARGET_HZ.
 *
 * No printf from here: stdio belongs to core0 and one printf costs more than the
 * sample it would describe. No cyw43_arch_* either - that chip is core0's. */
static void core1_producer(void) {
    pace_init();

    while (g_running) {
        daq_sample_t s;
        sample_read(&s);

        if (!queue_try_add(&g_queue, &s)) g_dropped++;

        pace_wait();
    }

    /* core0 calls multicore_reset_core1() before the next launch */
    while (1) tight_loop_contents();
}
#endif /* DAQ_USE_CORE1 */

/* ==========================================================================
 * Capture
 * ========================================================================== */

static bool stop_requested(void) {
    int c = getchar_timeout_us(0);
    return (c == 'x' || c == 'X');
}

static void capture_report(uint32_t samples, uint32_t dropped, uint32_t t_first, uint32_t t_last) {
    float seconds = (samples > 1) ? ((float)(t_last - t_first) / 1e6f) : 0.0f;
    float rate    = (seconds > 0.0f) ? ((float)samples / seconds) : 0.0f;

    g_last_samples = samples;
    g_last_dropped = dropped;
    g_last_seconds = seconds;
    g_last_rate    = rate;

    printf("DONE file=%s samples=%lu dropped=%lu seconds=%.3f rate=%.1f Hz\n",
           g_path, (unsigned long)samples, (unsigned long)dropped,
           (double)seconds, (double)rate);
}

static void capture_run(void) {
    if (!next_free_path()) { g_run_failed = true; return; }

    FRESULT fr = f_open(&g_file, g_path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("f_open(%s) -> %s (%d)\n", g_path, FRESULT_str(fr), fr);
        g_run_failed = true;
        return;
    }

#if !DAQ_BINARY
    UINT bw = 0;
    fr = f_write(&g_file, DAQ_CSV_HEADER, (UINT)strlen(DAQ_CSV_HEADER), &bw);
    if (fr != FR_OK) {
        printf("header f_write -> %s (%d)\n", FRESULT_str(fr), fr);
        f_close(&g_file);
        g_run_failed = true;
        return;
    }
#endif

    sink_reset();
    g_dropped = 0;

    uint32_t samples = 0, t_first = 0, t_last = 0;
    bool failed = false;

    printf("CAPTURE start file=%s target=%d Hz for %d s ('x' stops early)\n",
           g_path, DAQ_TARGET_HZ, DAQ_RUN_SECONDS);
    led_tick(LED_CAPTURE);

    absolute_time_t deadline = make_timeout_time_ms(DAQ_RUN_SECONDS * 1000);

#if DAQ_USE_CORE1
    /* ---- two-core configuration: core1 samples, core0 writes ---- */
    daq_sample_t drain;
    while (queue_try_remove(&g_queue, &drain)) { /* leftovers from a previous run */ }

    g_running = true;
    multicore_reset_core1();
    multicore_launch_core1(core1_producer);

    /* Poll the stop command on a time base, not on a loop counter. This loop spins
     * free whenever the queue is empty, so a counter would call into USB stdio tens
     * of thousands of times a second on the same core that is driving the card. */
    absolute_time_t next_poll = make_timeout_time_ms(20);

    while (g_running) {
        daq_sample_t s;
        if (queue_try_remove(&g_queue, &s)) {
            if (!sink_push_ok(&s, samples)) { failed = true; break; }
            if (samples == 0) t_first = s.t_us;
            t_last = s.t_us;
            samples++;
        }
        if (time_reached(deadline)) break;
        if (time_reached(next_poll)) {
            next_poll = make_timeout_time_ms(20);
            if (stop_requested()) break;
        }
    }

    /* Stop the producer, then empty the queue - which is also what releases a
     * producer that is blocked inside queue_add_blocking(). */
    g_running = false;
    for (int pass = 0; pass < 2 && !failed; pass++) {
        daq_sample_t s;
        while (queue_try_remove(&g_queue, &s)) {
            if (!sink_push_ok(&s, samples)) { failed = true; break; }
            if (samples == 0) t_first = s.t_us;
            t_last = s.t_us;
            samples++;
        }
        sleep_ms(5);
    }
#else
    /* ---- single-core configuration: sample and write in the same loop ----
     * This path is complete and is your baseline. Every capture you measure
     * later is measured against the number this one prints. */
    g_running = true;
    pace_init();

    uint32_t poll = 0;
    while (g_running) {
        daq_sample_t s;
        sample_read(&s);

        if (!sink_push_ok(&s, samples)) { failed = true; break; }
        if (samples == 0) t_first = s.t_us;
        t_last = s.t_us;
        samples++;

        if (time_reached(deadline)) break;
        if ((++poll & 0x3Fu) == 0 && stop_requested()) break;

        pace_wait();
    }
    g_running = false;
#endif

    if (!failed) {
        fr = sink_flush();
        if (fr != FR_OK) { printf("sink_flush -> %s (%d)\n", FRESULT_str(fr), fr); failed = true; }
    }

    f_close(&g_file);
    g_run_failed = failed;

    capture_report(samples, g_dropped, t_first, t_last);
}

/* ==========================================================================
 * Banner / info
 * ========================================================================== */

static void print_config(void) {
    printf("config: core1=%d binary=%d block=%d sync=%d target=%d Hz run=%d s queue=%d\n",
           DAQ_USE_CORE1, DAQ_BINARY, DAQ_BLOCK_SAMPLES, DAQ_SYNC_BLOCKS,
           DAQ_TARGET_HZ, DAQ_RUN_SECONDS, DAQ_QUEUE_LEN);
    printf("        record=%u B  block buffer=%u B  period=%u us  format=%s\n",
           (unsigned)DAQ_RECORD_MAX, (unsigned)DAQ_BLOCK_BYTES,
           (unsigned)DAQ_PERIOD_US, DAQ_BINARY ? "packed binary" : "CSV text");
}

static void print_info(void) {
    print_config();
    printf("last run: samples=%lu dropped=%lu seconds=%.3f rate=%.1f Hz file=%s\n",
           (unsigned long)g_last_samples, (unsigned long)g_last_dropped,
           (double)g_last_seconds, (double)g_last_rate,
           g_path[0] ? g_path : "(none)");
    printf("state: boot=%s last-run=%s late=%lu\n",
           g_boot_fault ? "FAULT" : "ok", g_run_failed ? "FAILED" : "ok",
           (unsigned long)g_late);
}

/* ==========================================================================
 * main
 * ========================================================================== */

int main(void) {
    stdio_init_all();
    sleep_ms(2000);                     /* let the host enumerate the CDC port */

    printf("\n=== IAS0360 lab_1_1 daq ===\n");
    print_config();

    if (!hardware_init()) g_boot_fault = true;

    /* cyw43 AFTER the SD card. Both claim PIO state machines and DMA channels at
     * init time, and cyw43_claim takes whatever is free. Bringing cyw43 up first
     * left cyw43_arch_gpio_put() blocking forever on the first call made after
     * sd_init_driver() ran. Initialising it last avoids the contention. */
    if (cyw43_arch_init()) {
        printf("cyw43_arch_init() failed - no LED, continuing\n");
    }
    led_put(false);

#if DAQ_USE_CORE1
    queue_init(&g_queue, sizeof(daq_sample_t), DAQ_QUEUE_LEN);
#endif

    printf("commands: s=start  x=stop  i=info  l=list  d=dump\n");
    printf("READY\n");

    while (true) {
        led_tick(fault_active() ? LED_ERROR : LED_IDLE);
        { static absolute_time_t hb = {0};
          if (time_reached(hb)) { hb = make_timeout_time_ms(2000); printf("HB alive\n"); } }

        int c = getchar_timeout_us(1000);
        if (c != PICO_ERROR_TIMEOUT) printf("RX %d\n", c);
        switch (c) {
            case 's': case 'S':
                /* Re-probe only after a fault, so a healthy board starts instantly.
                 * imuInit() re-measures the gyro offset and costs about 350 ms. */
                if (fault_active()) {
                    printf("re-init after error...\n");
                    g_boot_fault = !hardware_init();
                }
                if (!g_boot_fault && g_drive) {
                    g_run_failed = false;
                    capture_run();
                } else {
                    printf("hardware not ready - capture skipped\n");
                }
                break;
            case 'x': case 'X':
                printf("not capturing\n");
                break;
            case 'i': case 'I':
                print_info();
                break;
            case 'l': case 'L':
                if (g_drive) list_files();
                else         printf("no SD card mounted\n");
                break;
            case 'd': case 'D':
                dump_file();
                break;
            default:
                break;                  /* PICO_ERROR_TIMEOUT and anything else */
        }
    }
}
