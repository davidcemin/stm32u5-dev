/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_MGMT_H
#define EMW3080_MGMT_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/wifi_mgmt.h>

/* Work item to deliver scan results */
extern struct k_work_delayable deliver_scan_result_work;

/* WiFi management API functions */
int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb);
int emw3080_mgmt_connect(const struct device *dev, struct wifi_connect_req_params *params);
int emw3080_mgmt_disconnect(const struct device *dev);
int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status);

/* Initialize the WiFi management interface */
void emw3080_mgmt_init(void);

/* Set the network interface for management functions */
void emw3080_mgmt_set_iface(struct net_if *net_iface);

#endif /* EMW3080_MGMT_H */
