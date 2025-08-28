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
#include <zephyr/net/net_l2.h>
#include <stdbool.h>

#include "emw3080_offload_dev.h"
#include "emw3080_mgmt.h"
#include "emw3080_l2.h"

/* Forward declarations */
extern int emw3080_init_with_uart(const struct device *dev, const struct device *uart_dev);
extern const struct net_offload emw3080_offload;

/* Forward declarations of internal functions - Must be at the top! */
static void emw3080_net_iface_init(struct net_if *iface);
static enum offloaded_net_if_types emw3080_get_type(void);
static int emw3080_enable(const struct net_if *iface, bool state);
static int emw3080_net_device_init(const struct device *dev);
static int emw3080_scan(const struct device *dev, struct wifi_scan_params *params, scan_result_cb_t cb);
static int emw3080_connect(const struct device *dev, struct wifi_connect_req_params *params);
static int emw3080_disconnect(const struct device *dev);
static int emw3080_get_status(const struct device *dev, struct wifi_iface_status *status);

/* We'll use Ethernet L2 as the base for our implementation */

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

/* Define the WiFi management operations */
static const struct wifi_mgmt_ops emw3080_wifi_mgmt_ops = {
    .scan = emw3080_scan,
    .connect = emw3080_connect,
    .disconnect = emw3080_disconnect,
    .iface_status = emw3080_get_status,
};

/* Define the proper interface API with get_type implementation for WiFi detection */
static const struct offloaded_if_api offloaded_if = {
    .iface_api.init = emw3080_net_iface_init,
    .get_type = emw3080_get_type,  /* This is critical for WiFi identification */
    .enable = emw3080_enable,
};

/* WiFi management offload structure - this is exported for external code */
const struct net_wifi_mgmt_offload emw3080_mgmt_api = {
    .wifi_iface = {
        .iface_api.init = emw3080_net_iface_init,
        .get_type = emw3080_get_type,
        .enable = emw3080_enable,
    },
    .wifi_mgmt_api = &emw3080_wifi_mgmt_ops,
};

/* Function to identify this device as WiFi - crucial for proper identification */
static enum offloaded_net_if_types emw3080_get_type(void)
{
    LOG_INF("get_type called, reporting device as L2_OFFLOADED_NET_IF_TYPE_WIFI (%d)", 
           L2_OFFLOADED_NET_IF_TYPE_WIFI);
    return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

/* Function to enable/disable the interface */
static int emw3080_enable(const struct net_if *iface, bool state)
{
    LOG_INF("Enable/disable interface: %s", state ? "UP" : "DOWN");
    return 0;  /* Always successful for now */
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

/* Function to initialize the interface */
static void emw3080_net_iface_init(struct net_if *iface)
{
    const struct device *dev = net_if_get_device(iface);
    struct emw3080_net_data *data = dev->data;
    
    LOG_INF("EMW3080 interface initialized");
    
    /* Verify the device API pointer */
    LOG_INF("Device API is at %p", dev->api);
    
    /* Check if the API structure has our get_type function */
    const struct offloaded_if_api *api = (const struct offloaded_if_api *)dev->api;
    if (api && api->get_type) {
        LOG_INF("Device API has get_type function - GOOD");
        enum offloaded_net_if_types type = api->get_type();
        LOG_INF("API get_type() returns: %d (WiFi=%d)", 
               type, (type == L2_OFFLOADED_NET_IF_TYPE_WIFI) ? 1 : 0);
    } else {
        LOG_ERR("Device API does not have valid get_type function!");
    }
    
    /* Save reference to interface */
    data->iface = iface;
    
    /* Set the interface in the WiFi management module */
    emw3080_mgmt_set_iface(iface);
    
    /* Initialize WiFi management functionality - critical for WiFi identification */
    emw3080_mgmt_init();
    
    /* IMPORTANT: Set up interface properties - this is critical for DHCP to work */
    LOG_INF("Setting up interface properties for EMW3080");
    
    /* Set MAC address for the interface */
    net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_ETHERNET);
    
    /* Attach our L2 implementation to the interface */
    emw3080_attach_l2_to_iface(iface);
    
    /* Register the offload API directly with the interface.
     * In modern Zephyr versions, this is done during NET_DEVICE_OFFLOAD_INIT.
     */
    LOG_INF("Offload API registered during device initialization");
    
    /* Set flags to enable sending/receiving */
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    
    LOG_INF("Interface setup complete");
    
    /* Check if the interface is identified as WiFi and has L2 */
    int is_wifi = net_if_is_wifi(iface);
    int is_offloaded_wifi = net_off_is_wifi_offloaded(iface);
    struct net_l2 *l2 = net_if_l2(iface);
    
    LOG_INF("EMW3080 WiFi interface registered and ready. WiFi=%d, Offloaded WiFi=%d, L2=%p",
           is_wifi, is_offloaded_wifi, l2);
}

/* Basic initialization function for the network interface */
static int emw3080_net_device_init(const struct device *dev)
{
    LOG_INF("EMW3080 network device initializing");
    
    /* Log device API info */
    LOG_INF("Device API address: %p", dev->api);
    
    /* Check our get_type function result */
    enum offloaded_net_if_types type = emw3080_get_type();
    LOG_INF("Our get_type function returns: %d (WiFi=%d)", 
           type, (type == L2_OFFLOADED_NET_IF_TYPE_WIFI) ? 1 : 0);
    
    /* Initialize our L2 layer */
    emw3080_l2_init();
    
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

/* Create the network device - this is the registration that integrates 
 * our driver with the Zephyr networking subsystem.
 * 
 * We use NET_DEVICE_OFFLOAD_INIT to explicitly register this as an offloaded network device
 * that relies on &offloaded_if API, which contains both the interface init function 
 * and the get_type function needed for WiFi identification.
 * 
 * The L2 layer will be registered during interface initialization in emw3080_net_iface_init.
 */
NET_DEVICE_OFFLOAD_INIT(emw3080_net,                /* Driver name */
                       "EMW3080_NET",              /* Device name */
                       emw3080_net_device_init,    /* Init function */
                       NULL,                       /* PM control */
                       &emw3080_net_dev_data,      /* Data */
                       NULL,                       /* Config */
                       CONFIG_WIFI_INIT_PRIORITY,  /* Priority */
                       &offloaded_if,              /* API */
                       1500);                      /* MTU */

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
