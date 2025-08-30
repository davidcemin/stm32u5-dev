/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_stubs, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

/* Stub implementations for temporarily disabled functions */

void emw3080_debug_list_devices(void)
{
    LOG_INF("EMW3080 Debug: List devices (stub implementation)");
}

void emw3080_debug_list_interfaces(void)
{
    LOG_INF("EMW3080 Debug: List interfaces (stub implementation)");
}

void emw3080_debug_check_initialization(void)
{
    LOG_INF("EMW3080 Debug: Check initialization (stub implementation)");
}

int emw3080_fallback_init(void)
{
    LOG_INF("EMW3080 Fallback: Init (stub implementation)");
    return 0;
}

int emw3080_l2_init(void)
{
    LOG_INF("EMW3080 L2: Init (stub implementation)");
    return 0;
}

int emw3080_attach_l2_to_iface(struct net_if *iface)
{
    LOG_INF("EMW3080 L2: Attach L2 to interface (stub implementation)");
    return 0;
}

void emw3080_debug_at_commands(void)
{
    LOG_INF("EMW3080 Debug: AT commands (stub implementation)");
}

/* WiFi management stub functions */
int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb)
{
    LOG_INF("EMW3080 Management: Scan (stub implementation)");
    return -ENOTSUP;
}

bool emw3080_mgmt_scan_results_ready(void)
{
    LOG_INF("EMW3080 Management: Scan results ready (stub implementation)");
    return false;
}

int emw3080_mgmt_get_scan_results(struct wifi_scan_result *results, int max_results, int *count)
{
    LOG_INF("EMW3080 Management: Get scan results (stub implementation)");
    if (count) *count = 0;
    return 0;
}

int emw3080_mgmt_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
    LOG_INF("EMW3080 Management: Connect (stub implementation)");
    return -ENOTSUP;
}

int emw3080_mgmt_disconnect(const struct device *dev)
{
    LOG_INF("EMW3080 Management: Disconnect (stub implementation)");
    return -ENOTSUP;
}

int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status)
{
    LOG_INF("EMW3080 Management: Get status (stub implementation)");
    if (status) {
        status->state = WIFI_STATE_DISCONNECTED;
    }
    return 0;
}

void emw3080_mgmt_set_iface(struct net_if *iface)
{
    LOG_INF("EMW3080 Management: Set interface (stub implementation)");
}

int emw3080_mgmt_init(void)
{
    LOG_INF("EMW3080 Management: Init (stub implementation)");
    return 0;
}

/* L2 symbols that might be needed */
struct net_l2 EMW3080_L2 = {
    .recv = NULL,
    .send = NULL,
    .enable = NULL,
    .get_flags = NULL,
};

/* Export the L2 symbol */
struct net_l2 *_net_l2_EMW3080_L2 = &EMW3080_L2;
