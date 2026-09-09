#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Read-only host Wi-Fi status. This never connects, disconnects, or scans. */
bool sim_host_wifi_get_current(char *ssid, size_t ssid_size);
