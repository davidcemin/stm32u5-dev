/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_net, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/wifi_mgmt.h>

#include "emw3080_offload_dev.h"

/* Forward declarations from emw3080.c */
extern const struct net_wifi_mgmt_offload emw3080_api;
extern int emw3080_init_with_uart(const struct device *dev, const struct device *uart_dev);

/* Define data structure for network driver */
struct emw3080_net_data {
    uint8_t mac_addr[6];
    const struct device *uart;
    struct net_if *iface;
};

/* Static instance for the network device */
static struct emw3080_net_data emw3080_net_dev_data = {
    .mac_addr = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 },
};

/* Basic initialization function for the network interface */
static int emw3080_net_device_init(const struct device *dev)
{
    LOG_INF("EMW3080 network device initializing");
    
    /* Directly get UART4 */
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart4));
    if (!uart || !device_is_ready(uart)) {
        LOG_ERR("UART4 not available");
        return -ENODEV;
    }
    
    struct emw3080_net_data *data = dev->data;
    data->uart = uart;
    
    /* This initialization will be called again by the networking stack */
    LOG_INF("EMW3080 network device ready");
    return 0;
}

/* Function to initialize the interface */
static void emw3080_net_iface_init(struct net_if *iface)
{
    const struct device *dev = net_if_get_device(iface);
    struct emw3080_net_data *data = dev->data;
    
    LOG_INF("EMW3080 interface initialized");
    
    /* Set MAC address */
    net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_ETHERNET);
    
    /* Save reference to interface */
    data->iface = iface;
    
    /* Let the network stack know we're up */
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
}

/* WiFi management API implementations */
static int emw3080_scan(const struct device *dev, scan_result_cb_t cb)
{
    LOG_INF("WiFi scan requested");
    return -ENOTSUP; /* Not yet implemented */
}

static int emw3080_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
    LOG_INF("WiFi connect requested");
    return -ENOTSUP; /* Not yet implemented */
}

static int emw3080_disconnect(const struct device *dev)
{
    LOG_INF("WiFi disconnect requested");
    return -ENOTSUP; /* Not yet implemented */
}

/* Network interface API */
static const struct net_if_api emw3080_net_if_api = {
    .init = emw3080_net_iface_init,
};

/* Network offload API from emw3080_offload.c */
extern const struct net_offload emw3080_offload;

/* WiFi management offload structure */
const struct net_wifi_mgmt_offload emw3080_mgmt_api = {
    .wifi_iface.iface_api = emw3080_net_if_api,
};

/* Create the network device */
NET_DEVICE_OFFLOAD_INIT(emw3080_net,
                        "EMW3080_NET",
                        emw3080_net_device_init,
                        NULL,
                        &emw3080_net_dev_data,
                        NULL,
                        CONFIG_WIFI_INIT_PRIORITY,
                        &emw3080_mgmt_api.wifi_iface,
                        1500);  /* MTU */

/* Function to be called from main to check if network device is ready */
const struct device *get_emw3080_net_device(void)
{
    static const struct device *dev;
    
    if (!dev) {
        dev = device_get_binding("EMW3080_NET");
        if (!dev) {
            LOG_ERR("EMW3080_NET device not found");
        } else {
            LOG_INF("EMW3080_NET device found");
        }
    }
    
    return dev;
}
