#include <stdio.h>
#include <vector>
#include <cstring>
#include <sstream>

#include "pico/stdlib.h"
#include "pico/platform.h"
#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/timer.h"

#include "networking_main.h"
#include "request.hpp"

static inline void debug_unpause_timer(void) {
    timer_hw->dbgpause = 0;
}

// Connect WiFi
int wifi_connection_example(void) {

	stdio_init_all();
    debug_unpause_timer();

    sleep_ms(5000);

    if (cyw43_arch_init()) {
        printf("Wi-Fi chip init failed\n");
        return -1;
    }
    printf("Wi-Fi chip initialized successfully\n");

    cyw43_arch_enable_sta_mode();

    // Connect to the WiFI network - loop until connected
    printf("Start connecting wifi.\n");
    while(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) {
        printf("Attempting to connect...\n");
    }
    // Print a success message once connected
    printf("WiFi connected successfully! \n");

    sleep_ms(3000);
    return 0;
}

// Send GET request
int get_request_example(void) {
    const char* get_url = "/api/v1/healthcheck";
    HttpRequest request(get_url);
    ResponseDataStr response = request.get();
    if (!response.status_ok) {
        printf("GET Request failed\n");
        return -1;
    }
    printf("GET Response: %s \n", response.y);
    // Expected output: "healthy"
    free(response.y);
    return 0;
}

// Send POST request
int post_request_example(void) {
    const char* post_url = "/api/v1/greet";
    HttpRequest request(post_url);

    std::string json;
    json.append("{\"name\": \"");
    std::string name = "Raspberry Pi Pico W";
    json.append(name);
    json.append("\"}");

    request.set_body_raw(json.c_str(), json.size());

    ResponseDataStr response = request.post();
    if (!response.status_ok) {
        printf("POST Request failed\n");
        return -1;
    }
    printf("POST Response: %s \n", response.y);
    // Expected output: "Hello, Raspberry Pi Pico W!"
    free(response.y);
    return 0;
}

// POST /api/v1/predict_a with {"x": <float32>}; the server runs y = 2x + 1
int post_request_with_float_data(float x) {
    const char* post_url = "/api/v1/predict_a";
    HttpRequest request(post_url);

    char json[48];
    int len = snprintf(json, sizeof json, "{\"x\": %f}", x);
    request.set_body_raw(json, len);

    ResponseDataStr response = request.post();
    if (!response.status_ok) {
        printf("POST Request failed\n");
        return -1;
    }
    printf("POST x=%f -> %s (expected %f)\n", x, response.y, 2.0f * x + 1.0f);
    free(response.y);
    return 0;
}

int main(void)
{
    if (wifi_connection_example() != 0) return -1;

    get_request_example();
    sleep_ms(500);
    post_request_example();

    const float xs[] = {3.14f, -1.5f, 0.0f, 42.0f};
    for (float x : xs) {
        sleep_ms(500);
        post_request_with_float_data(x);
    }

    printf("Done.\n");
    while (1) tight_loop_contents();
    return 0;
}
