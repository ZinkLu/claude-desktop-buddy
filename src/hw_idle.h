#pragma once
#include <stdint.h>

void hw_idle_init();
void hw_idle_activity();           // Reset idle timer (call on any input)
bool hw_idle_tick(uint32_t now);   // Check timeout, returns true if just entered dim
bool hw_idle_is_dimmed();          // Query current dim state
void hw_idle_set_enabled(bool en); // Enable/disable feature
