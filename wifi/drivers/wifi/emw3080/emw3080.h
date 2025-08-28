/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_H_
#define ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_H_

#include <zephyr/kernel.h>

/* Forward declarations */
static void emw3080_request_handler(struct k_work *work);

/* Network offloading API functions (implemented in emw3080_offload.c) */
extern const struct net_offload emw3080_offload;

#endif /* ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_H_ */
