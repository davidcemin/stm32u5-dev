/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_l2, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_l2.h>

#include "emw3080_mgmt.h"

/* Define a dummy MAC address for now */
static uint8_t emw3080_mac_addr[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

/* Custom L2 interface for EMW3080 */
static enum net_verdict emw3080_l2_recv(struct net_if *iface, struct net_pkt *pkt)
{
    LOG_DBG("L2 recv function called");
    
    /* Basic packet validation */
    if (!pkt) {
        LOG_ERR("Received NULL packet");
        return NET_DROP;
    }
    
    /* Forward to upper layer */
    return NET_CONTINUE;
}

static int emw3080_l2_send(struct net_if *iface, struct net_pkt *pkt)
{
    LOG_DBG("L2 send function called");
    
    if (!iface || !pkt) {
        LOG_ERR("Invalid parameters: iface=%p, pkt=%p", iface, pkt);
        return -EINVAL;
    }
    
    /* Log that we were called - this would be replaced with real sending logic */
    LOG_INF("L2 send: packet size=%d bytes", net_pkt_get_len(pkt));
    
    /* In a real implementation, we would call the device's send function */
    /* For now, just pretend we sent it successfully */
    return 0;
}

/* Define our L2 interface structure */
NET_L2_DECLARE_PUBLIC(EMW3080_L2);
NET_L2_INIT(EMW3080_L2, emw3080_l2_recv, emw3080_l2_send, NULL, NULL);

/* Function to attach the L2 interface to the WiFi interface */
int emw3080_attach_l2_to_iface(struct net_if *iface)
{
    LOG_INF("Attaching L2 to WiFi interface");
    
    if (!iface) {
        LOG_ERR("No interface provided");
        return -EINVAL;
    }
    
    /* Set MAC address for the interface */
    net_if_set_link_addr(iface, emw3080_mac_addr, sizeof(emw3080_mac_addr), NET_LINK_ETHERNET);
    
    /* Set the L2 for this interface - THIS IS CRITICAL */
    net_if_set_l2(iface, &NET_L2_GET_NAME(EMW3080_L2));
    
    /* Mark interface as UP and RUNNING */
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    
    LOG_INF("L2 interface attached successfully");
    
    return 0;
}

/* Function to enable direct communication with EMW3080 */
int emw3080_enable_direct_mode(struct net_if *iface)
{
    LOG_INF("Enabling direct mode for EMW3080");
    
    if (!iface) {
        LOG_ERR("No interface provided");
        return -EINVAL;
    }
    
    /* This function would normally configure the EMW3080 for direct packet transfer */
    /* For now, just set up the basic network parameters */
    
    /* Set flags to enable sending/receiving */
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    
    LOG_INF("Direct mode enabled");
    
    return 0;
}
