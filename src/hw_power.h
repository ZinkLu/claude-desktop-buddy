#pragma once
#include <time.h>

void    hw_power_init();
void    hw_power_off();   // Drive ON_OFF_PIN low to cut main regulator
time_t  hw_power_now();   // time(nullptr); valid once BLE has synced RTC
