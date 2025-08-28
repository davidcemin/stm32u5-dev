/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_DEBUG_H_
#define ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_DEBUG_H_

/* Debug function prototypes */
void emw3080_debug_list_devices(void);
void emw3080_debug_list_interfaces(void);
void emw3080_debug_check_initialization(void);
int emw3080_debug_at_commands(void);

#endif /* ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_DEBUG_H_ */
