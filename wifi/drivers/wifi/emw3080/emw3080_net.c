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
#include <zephyr/sys/printk.h>
#include <stdbool.h>

#include "emw3080_offload_dev.h"
#include "emw3080_mgmt.h"
#include "emw3080_l2.h"
#include "emw3080_offload.h"
#include "emw3080_ipc.h"

/* Include the L2 implementations we need */
NET_L2_DECLARE_PUBLIC(OFFLOADED_NETDEV);
/* EMW3080_L2 is declared and defined in emw3080_l2.c - rely on linker */

/* We'll use the L2 implementation directly instead of trying to set offload */

/* Forward declarations */
extern int emw3080_init_with_spi(const struct device *dev, const struct device *spi_dev);

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
    
    /* MAKE SURE THIS APPEARS IN LOGS */
    printk("!!! EMW3080_NET_IFACE_INIT CALLED !!! Interface: %p\n", iface);
    LOG_ERR("!!! EMW3080_NET_IFACE_INIT CALLED !!! Interface: %p", iface);
    
    LOG_INF("EMW3080: Interface initialization started for iface %p (idx=%d)", 
           iface, net_if_get_by_iface(iface));
    
    /* CRITICAL: Register the network offload API using the proper Zephyr method */
    LOG_INF("EMW3080: Registering network offload API");
    /* Cast away const to set the offload API - this is how it's done in Zephyr examples */
    ((struct net_if_dev *)iface->if_dev)->offload = &emw3080_offload;
    
    /* CRITICAL: Set the offload flags to ensure packets go through offload path */
    LOG_INF("EMW3080: Setting offload flags");
    net_if_flag_set(iface, NET_IF_IPV4);
    net_if_flag_set(iface, NET_IF_IPV6);
    
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
    
    /* Set MAC address for the interface */
    LOG_INF("EMW3080: Setting MAC address: %02x:%02x:%02x:%02x:%02x:%02x", 
           data->mac_addr[0], data->mac_addr[1], data->mac_addr[2],
           data->mac_addr[3], data->mac_addr[4], data->mac_addr[5]);
    net_if_set_link_addr(iface, data->mac_addr, sizeof(data->mac_addr), NET_LINK_ETHERNET);
    
    /* Check the L2 implementation */
    const struct net_l2 *l2_interface = net_if_l2(iface);
    LOG_INF("EMW3080: L2 interface: %p", l2_interface);
           
    if (l2_interface && l2_interface->send) {
        LOG_INF("EMW3080: L2 send function available: %p", l2_interface->send);
    } else {
        LOG_INF("EMW3080: No L2 send function - this will cause 'l2 cannot send' error");
        
        /* CRITICAL FIX: The OFFLOADED_NETDEV L2 layer has NULL send function,
         * which causes "l2 for iface 1 cannot send, discard pkt" error.
         * We need to override it with our custom L2 layer that has a send function.
         */
        
        /* Get our custom L2 layer */
        extern const struct net_l2 NET_L2_GET_NAME(EMW3080_L2);
        const struct net_l2 *our_l2 = &NET_L2_GET_NAME(EMW3080_L2);
        
        LOG_INF("EMW3080: Our L2 implementation: %p", our_l2);
        LOG_INF("EMW3080: Our L2 send function: %p", our_l2->send);
        
        /* Override the L2 layer pointer in the interface device.
         * This is a workaround since the L2 pointer is const, but we need
         * to provide a send function to prevent packet drops.
         */
        struct net_if_dev *if_dev = (struct net_if_dev *)iface->if_dev;
        /* Cast away const to modify the L2 pointer */
        *((const struct net_l2 **)&if_dev->l2) = our_l2;
        
        LOG_INF("EMW3080: Successfully assigned our L2 layer to interface");
        
        /* Verify the assignment worked */
        const struct net_l2 *new_l2 = net_if_l2(iface);
        if (new_l2 && new_l2->send) {
            LOG_INF("EMW3080: ✓ L2 send function now available: %p", new_l2->send);
        } else {
            LOG_ERR("EMW3080: ✗ Failed to assign L2 send function");
        }
    }
    
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
    
    /* Check if the interface is offloaded and has proper API */
    bool is_offloaded = net_if_is_offloaded(iface);
    LOG_INF("EMW3080: Interface is offloaded: %d", is_offloaded);
    
    if (is_offloaded) {
        /* Check offload API registration - this should be available now */
        const struct net_offload *offload_api = net_if_offload(iface);
        if (offload_api) {
            LOG_INF("EMW3080: Offload API registered - send: %p, sendto: %p", 
                   offload_api->send, offload_api->sendto);
            LOG_INF("EMW3080: SUCCESS - Offload API properly registered!");
        } else {
            LOG_ERR("EMW3080: FAILED - No offload API registered despite being offloaded!");
        }
    } else {
        LOG_ERR("EMW3080: FAILED - Interface is not marked as offloaded!");
        LOG_ERR("EMW3080: This means packets will try to use L2 send (which doesn't exist)");
        
        /* Force-set the offload API again with more aggressive approach */
        LOG_INF("EMW3080: Attempting force registration of offload API");
        struct net_if_dev *if_dev = (struct net_if_dev *)iface->if_dev;
        if_dev->offload = &emw3080_offload;
        
        /* Double-check if it worked */
        is_offloaded = net_if_is_offloaded(iface);
        LOG_INF("EMW3080: After force registration, is_offloaded: %d", is_offloaded);
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
    
    /* MANUALLY CALL INTERFACE INIT - Since automatic isn't working */
    LOG_INF("EMW3080: MANUALLY calling interface initialization after device init");
    
    /* Find the network interface for this device and call init */
    /* We'll do this in a work queue since interfaces might not be ready yet */
    return 0;
}

/* Create the network device - this is the registration that integrates 
 * our driver with the Zephyr networking subsystem.
 * 
 * We use NET_DEVICE_OFFLOAD_INIT to register as an offloaded network device.
 * The L2 send function issue will be resolved by manually setting up the
 * interface's L2 layer during initialization.
 */
/* Use the standard offload device registration */
NET_DEVICE_OFFLOAD_INIT(emw3080_net,               /* Driver name */
                       "EMW3080_NET",              /* Device name */
                       emw3080_net_device_init,    /* Init function */
                       NULL,                       /* PM control */
                       &emw3080_net_dev_data,      /* Data */
                       NULL,                       /* Config */
                       CONFIG_WIFI_INIT_PRIORITY,  /* Priority */
                       &offloaded_if,              /* Our custom API */
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
