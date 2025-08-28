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
#include <zephyr/net/offloaded_netdev.h>

#include "emw3080_offload_dev.h"
#include "emw3080_mgmt.h"

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
    
    /* Set the interface in the WiFi management module */
    emw3080_mgmt_set_iface(iface);
    
    /* Let the network stack know we're up */
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
}

/* WiFi management API implementations - delegates to emw3080_mgmt module */
static int emw3080_scan(const struct device *dev, struct wifi_scan_params *params,
                       scan_result_cb_t cb)
{
    LOG_INF("WiFi scan requested, delegating to mgmt module");
    return emw3080_mgmt_scan(dev, params, cb);
}

static int emw3080_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
    LOG_INF("WiFi connect requested, delegating to mgmt module");
    return emw3080_mgmt_connect(dev, params);
}

static int emw3080_disconnect(const struct device *dev)
{
    LOG_INF("WiFi disconnect requested, delegating to mgmt module");
    return emw3080_mgmt_disconnect(dev);
}

static int emw3080_get_status(const struct device *dev, struct wifi_iface_status *status)
{
    LOG_INF("WiFi get status requested, delegating to mgmt module");
    return emw3080_mgmt_get_status(dev, status);
}

/* Function to identify this device as WiFi */
static enum offloaded_net_if_types emw3080_get_type(void)
{
    return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

/* Network interface API with offloaded capabilities */
static const struct offloaded_if_api emw3080_offload_if_api = {
    .iface_api.init = emw3080_net_iface_init,
    .get_type = emw3080_get_type,
    /* We don't need enable/disable since we do that via .init */
};

/* Network offload API from emw3080_offload.c */
extern const struct net_offload emw3080_offload;

/* Define the WiFi management operations */
static const struct wifi_mgmt_ops emw3080_wifi_mgmt_ops = {
    .scan = emw3080_scan,
    .connect = emw3080_connect,
    .disconnect = emw3080_disconnect,
    .iface_status = emw3080_get_status,
};

/* WiFi management offload structure */
const struct net_wifi_mgmt_offload emw3080_mgmt_api = {
    .wifi_iface = emw3080_offload_if_api,
    .wifi_mgmt_api = &emw3080_wifi_mgmt_ops,
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
            /* Initialize WiFi management functionality */
            emw3080_mgmt_init();
        }
    }
    
    return dev;
}
