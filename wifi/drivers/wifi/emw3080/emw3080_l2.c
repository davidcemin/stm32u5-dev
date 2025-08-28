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
    LOG_INF("EMW3080 L2 recv function called for iface %p, pkt %p", iface, pkt);
    
    /* Basic packet validation */
    if (!pkt) {
        LOG_ERR("EMW3080 L2: Received NULL packet");
        return NET_DROP;
    }
    
    /* Log packet details */
    LOG_INF("EMW3080 L2 recv: packet size=%d bytes, iface=%p", 
           net_pkt_get_len(pkt), iface);
    
    /* Forward to upper layer */
    LOG_INF("EMW3080 L2 recv: Forwarding to upper layer");
    return NET_CONTINUE;
}

static int emw3080_l2_send(struct net_if *iface, struct net_pkt *pkt)
{
    LOG_INF("EMW3080 L2 send function called for iface %p, pkt %p", iface, pkt);
    
    if (!iface || !pkt) {
        LOG_ERR("EMW3080 L2 send: Invalid parameters: iface=%p, pkt=%p", iface, pkt);
        return -EINVAL;
    }
    
    /* Additional debug info */
    uint8_t *pkt_data = net_pkt_data(pkt);
    if (pkt_data) {
        LOG_INF("EMW3080 L2 send: First few bytes: %02x %02x %02x %02x %02x %02x",
               pkt_data[0], pkt_data[1], pkt_data[2], 
               pkt_data[3], pkt_data[4], pkt_data[5]);
    }
    
    /* Log packet context */
    LOG_INF("EMW3080 L2 send: packet size=%d bytes", net_pkt_get_len(pkt));
    LOG_INF("EMW3080 L2 send: packet iface=%p, expected iface=%p", 
           net_pkt_iface(pkt), iface);
    
    /* Check if the interface is up and running */
    LOG_INF("EMW3080 L2 send: iface UP=%d, RUNNING=%d", 
           net_if_flag_is_set(iface, NET_IF_UP),
           net_if_flag_is_set(iface, NET_IF_RUNNING));
    
    /* Use our implementation from emw3080_offload.c */
    LOG_INF("EMW3080 L2 send: Calling emw3080_send_pkt");
    int ret = emw3080_send_pkt(iface, pkt);
    
    if (ret < 0) {
        LOG_ERR("EMW3080 L2 send: Failed to send packet: error=%d", ret);
        return ret;
    }
    
    LOG_INF("EMW3080 L2 send: Successfully sent packet");
    return 0;
}

/* Function to attach the L2 interface to the WiFi interface */
int emw3080_attach_l2_to_iface(struct net_if *iface)
{
    LOG_INF("EMW3080: Attaching L2 to WiFi interface %p", iface);
    
    if (!iface) {
        LOG_ERR("EMW3080: No interface provided");
        return -EINVAL;
    }
    
    /* Log the interface index for debugging */
    LOG_INF("EMW3080: Interface index: %d", net_if_get_by_iface(iface));
    
    /* Set MAC address for the interface */
    LOG_INF("EMW3080: Setting interface MAC address");
    net_if_set_link_addr(iface, emw3080_mac_addr, sizeof(emw3080_mac_addr), NET_LINK_ETHERNET);
    LOG_INF("EMW3080: MAC address set: %02x:%02x:%02x:%02x:%02x:%02x", 
           emw3080_mac_addr[0], emw3080_mac_addr[1], emw3080_mac_addr[2],
           emw3080_mac_addr[3], emw3080_mac_addr[4], emw3080_mac_addr[5]);
    
    /* Check what L2 interface is being used */
    struct net_l2 *l2 = (struct net_l2 *)net_if_l2(iface);
    LOG_INF("EMW3080: Current L2 implementation: %p, our L2: %p", 
           l2, &NET_L2_GET_NAME(EMW3080_L2));
    
    if (l2 != &NET_L2_GET_NAME(EMW3080_L2)) {
        LOG_WRN("EMW3080: Interface not using EMW3080_L2, packets may be discarded");
        
        if (l2) {
            /* Check if the L2 interface has send capability */
            LOG_INF("EMW3080: Default L2 send function: %p", l2->send);
            if (l2->send == NULL) {
                LOG_ERR("EMW3080: Default L2 has no send function! This will cause packet drops.");
            } else {
                LOG_INF("EMW3080: Default L2 has a send function, will try to use it");
            }
        } else {
            LOG_ERR("EMW3080: Interface has no L2 implementation at all!");
        }
    } else {
        LOG_INF("EMW3080: Interface using correct EMW3080_L2 implementation");
        LOG_INF("EMW3080: Our L2 send function: %p", l2->send);
    }
    
    /* Mark interface as UP and RUNNING */
    LOG_INF("EMW3080: Setting interface UP and RUNNING flags");
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    LOG_INF("EMW3080: Interface flags set - UP=%d, RUNNING=%d", 
           net_if_flag_is_set(iface, NET_IF_UP),
           net_if_flag_is_set(iface, NET_IF_RUNNING));
    
    /* Check the offload status */
    LOG_INF("EMW3080: Checking if interface is offloaded");
    if (net_if_is_offloaded(iface)) {
        LOG_INF("EMW3080: Interface is properly marked as offloaded");
    } else {
        LOG_WRN("EMW3080: Interface is NOT marked as offloaded - this may cause issues");
    }
    
    LOG_INF("EMW3080: L2 interface setup complete");
    
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
    LOG_INF("EMW3080 L2 interface initialization started");
    
    /* Log information about our L2 implementation */
    const struct net_l2 *l2 = &NET_L2_GET_NAME(EMW3080_L2);
    LOG_INF("EMW3080 L2 details - Address: %p", l2);
    LOG_INF("EMW3080 L2 functions - recv: %p, send: %p", 
           l2->recv, l2->send);
    
    /* Enumerate all network interfaces and check their L2 implementations */
    struct net_if *ifaces[4]; /* Fixed size array for interfaces */
    size_t count = 0;
    
    /* Use net_if_get_by_index to find interfaces */
    struct net_if *iface;
    int i = 0;
    
    LOG_INF("EMW3080 L2: Enumerating network interfaces");
    
    /* Initialize array */
    for (i = 0; i < 4; i++) {
        ifaces[i] = NULL;
    }
    
    /* Get interfaces by index */
    for (i = 0; i < 4; i++) {
        iface = net_if_get_by_index(i + 1);  /* 1-based index */
        if (iface) {
            ifaces[count] = iface;
            count++;
        }
    }
    
    LOG_INF("EMW3080 L2: Found %d network interfaces", count);
    
    for (size_t i = 0; i < count && i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *iface = ifaces[i];
        LOG_INF("EMW3080 L2: Interface %d - Address: %p, L2: %p", 
               (int)i, iface, net_if_l2(iface));
        
        const struct net_l2 *iface_l2 = net_if_l2(iface);
        if (iface_l2) {
            LOG_INF("EMW3080 L2: Interface %d - L2 send: %p", 
                  (int)i, iface_l2->send);
            
            /* Check if it's a WiFi interface */
            if (net_if_is_wifi(iface)) {
                LOG_INF("EMW3080 L2: Interface %d is a WiFi interface", (int)i);
                
                /* Check if it's offloaded */
                if (net_if_is_offloaded(iface)) {
                    LOG_INF("EMW3080 L2: Interface %d is offloaded", (int)i);
                } else {
                    LOG_WRN("EMW3080 L2: WiFi interface %d is NOT offloaded!", (int)i);
                }
            }
        } else {
            LOG_WRN("EMW3080 L2: Interface %d has no L2 implementation!", (int)i);
        }
    }
    
    LOG_INF("EMW3080 L2 interface initialization completed");
}
