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

/* Forward declaration */
extern int emw3080_send_pkt(struct net_if *iface, struct net_pkt *pkt);

/* Define our L2 interface structure */
NET_L2_DECLARE_PUBLIC(EMW3080_L2);

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
    
    /* Log that we were called - use our emw3080_send_pkt implementation */
    LOG_INF("L2 send: packet size=%d bytes", net_pkt_get_len(pkt));
    
    /* Use our implementation from emw3080_offload.c */
    int ret = emw3080_send_pkt(iface, pkt);
    
    if (ret < 0) {
        LOG_ERR("Failed to send packet: %d", ret);
        return ret;
    }
    
    return 0;
}

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
    
    /* Check what L2 interface is being used */
    struct net_l2 *l2 = (struct net_l2 *)net_if_l2(iface);
    if (l2 != &NET_L2_GET_NAME(EMW3080_L2)) {
        LOG_WRN("Interface not using EMW3080_L2, packets may be discarded");
        LOG_WRN("We will use the interface's default L2: %p", l2);
        
        /* In newer Zephyr versions, we can't directly change the L2 implementation
         * after the interface is created. The L2 is specified during device registration
         * through the NET_DEVICE_INIT or NET_DEVICE_OFFLOAD_INIT macros.
         */
        if (l2 == NULL) {
            LOG_ERR("Interface has no L2 implementation at all!");
        } else {
            LOG_INF("Using interface's default L2 implementation");
        }
    } else {
        LOG_INF("Interface already using correct EMW3080_L2");
    }
    
    /* Mark interface as UP and RUNNING */
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    
    LOG_INF("L2 interface setup complete");
    
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

/* Initialize our L2 interface */
NET_L2_INIT(EMW3080_L2, emw3080_l2_recv, emw3080_l2_send, NULL, NULL);

/* This function needs to be called explicitly during initialization to
 * ensure our send function gets called when packets need to be sent.
 */
void emw3080_l2_init(void)
{
    LOG_INF("EMW3080 L2 interface initialized");
}
