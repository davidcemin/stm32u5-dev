/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_net, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/offloaded_netdev.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/ethernet.h>
#include <stdbool.h>

#include "emw3080_offload_dev.h"
#include "emw3080_mgmt.h"
#include "emw3080_l2.h"

/* Include the L2 implementations we need */
NET_L2_DECLARE_PUBLIC(OFFLOADED_NETDEV);
NET_L2_DECLARE_PUBLIC(EMW3080_L2);

/* We'll use the L2 implementation directly instead of trying to set offload */

/* Forward declarations */
extern int emw3080_init_with_spi(const struct device *dev, const struct device *spi_dev);
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
    const struct device *spi;
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
    
    LOG_INF("EMW3080: Interface initialization started for iface %p (idx=%d)", 
           iface, net_if_get_by_iface(iface));
    
    /* First step: Initialize our custom L2 implementation */
    LOG_INF("EMW3080: Initializing L2 layer");
    emw3080_l2_init();
    
    /* Save reference to interface */
    LOG_INF("EMW3080: Saving interface reference");
    data->iface = iface;
    
    /* Set the interface in the WiFi management module */
    LOG_INF("EMW3080: Setting interface in WiFi management module");
    emw3080_mgmt_set_iface(iface);
    
    /* Initialize WiFi management functionality - critical for WiFi identification */
    LOG_INF("EMW3080: Initializing WiFi management functionality");
    emw3080_mgmt_init();
    
    /* IMPORTANT: Set up interface properties - this is critical for DHCP to work */
    LOG_INF("EMW3080: Setting up interface properties");
    
    /* Set MAC address for the interface */
    LOG_INF("EMW3080: Setting MAC address: %02x:%02x:%02x:%02x:%02x:%02x", 
           data->mac_addr[0], data->mac_addr[1], data->mac_addr[2],
           data->mac_addr[3], data->mac_addr[4], data->mac_addr[5]);
    net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_ETHERNET);
    
    /* CRITICAL FIX: Check the L2 implementation and register our L2 functions */
    const struct net_l2 *l2_interface = net_if_l2(iface);
    LOG_INF("EMW3080: Current L2 interface: %p, EMW3080_L2: %p, OFFLOADED_NETDEV: %p",
           l2_interface, &NET_L2_GET_NAME(EMW3080_L2), &NET_L2_GET_NAME(OFFLOADED_NETDEV));
           
    if (l2_interface && l2_interface->send) {
        LOG_INF("EMW3080: L2 send function available: %p", l2_interface->send);
        
        /* Register our specific interface with the L2 implementation */
        LOG_INF("EMW3080: Explicitly registering our interface with the L2 layer");
    } else {
        LOG_WRN("EMW3080: Missing L2 send function! This will cause packet drops");
        LOG_WRN("EMW3080: Will attempt direct handling via emw3080_attach_l2_to_iface()");
    }
    
    /* Explicitly attach our L2 implementation to the interface */
    LOG_INF("EMW3080: Attaching custom L2 implementation to interface");
    emw3080_attach_l2_to_iface(iface);
    
    /* We'll rely on the L2 implementation directly rather than offload */
    LOG_INF("EMW3080: Using L2 implementation directly");
    
    /* Set flags to enable sending/receiving */
    LOG_INF("EMW3080: Setting interface flags UP and RUNNING");
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    
    /* Check if the flags were properly set */
    LOG_INF("EMW3080: Interface flags set - UP=%d, RUNNING=%d", 
           net_if_flag_is_set(iface, NET_IF_UP),
           net_if_flag_is_set(iface, NET_IF_RUNNING));
    
    /* Check if the interface is identified as WiFi */
    int is_wifi = net_if_is_wifi(iface);
    
    LOG_INF("EMW3080: WiFi interface registered. WiFi=%d", is_wifi);
    
    /* Double-check the L2 interface */
    const struct net_l2 *final_l2 = net_if_l2(iface);
    if (final_l2) {
        LOG_INF("EMW3080: Final L2 implementation - recv: %p, send: %p", 
               final_l2->recv, final_l2->send);
        
        if (final_l2->send == NULL) {
            LOG_ERR("EMW3080: Final L2 has NULL send function! DHCP will fail!");
        }
    } else {
        LOG_ERR("EMW3080: No L2 interface attached after setup - this will cause packet drops!");
    }
    
    LOG_INF("EMW3080: Interface initialization complete");
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
    
    /* Directly get SPI2 */
    const struct device *spi = DEVICE_DT_GET(DT_NODELABEL(spi2));
    if (!spi || !device_is_ready(spi)) {
        LOG_ERR("SPI2 not available");
        return -ENODEV;
    }
    
    struct emw3080_net_data *data = dev->data;
    data->spi = spi;
    
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
/* Use our custom L2 implementation directly */
/* Use the standard NET_DEVICE_INIT macro with our L2 API */
NET_DEVICE_OFFLOAD_INIT(emw3080_net,               /* Driver name */
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
