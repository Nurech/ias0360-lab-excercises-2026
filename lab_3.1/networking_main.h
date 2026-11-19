#ifndef MAIN_H
#define MAIN_H

// Real values go in wifi_secrets.h next to this file (git ignores it):
//   const char WIFI_SSID[] = "...";
//   const char WIFI_PASSWORD[] = "...";
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
const char WIFI_SSID[] = "your_wifi_ssid";
const char WIFI_PASSWORD[] = "your_wifi_password";
#endif

#endif // MAIN_H
