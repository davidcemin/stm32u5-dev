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
bool emw3080_mgmt_scan_results_ready(void);
int emw3080_mgmt_get_scan_results(struct wifi_scan_result *results, int max_results, int *count);
int emw3080_mgmt_connect(const struct device *dev, struct wifi_connect_req_params *params);
int emw3080_mgmt_disconnect(const struct device *dev);
int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status);

/* DHCP packet handling */
int emw3080_send_dhcp_packet(const struct device *dev, struct net_pkt *pkt);

/* Initialize the WiFi management interface */
void emw3080_mgmt_init(void);

/* Set the network interface for management functions */
void emw3080_mgmt_set_iface(struct net_if *net_iface);

/* EMW3080 IPC API */
int emw3080_ipc_init(const struct device *dev);
int emw3080_ipc_init_auto(void);  /* Auto-find device wrapper */

#endif /* EMW3080_MGMT_H */
