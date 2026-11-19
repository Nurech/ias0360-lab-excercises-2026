#ifndef CONTEXT_H
#define CONTEXT_H

#define CONTEXT_SWITCHER_OFFSET 0x100u
#define APP1_OFFSET 0x100u    // vector table of app1, right after its 256 B boot2
#define APP2_OFFSET 0x140000u // 1.25 MB, well above the size of app1, 4 KB aligned

#ifdef __cplusplus
extern "C" {
#endif
#include "pico/stdlib.h"
#include "hardware/structs/scb.h"   // scb_hw
#include "hardware/regs/xip.h"      // XIP_BASE
#include <stdint.h>

typedef void (*app_entry_t)(void);

void jump_to_image(uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif // CONTEXT_H