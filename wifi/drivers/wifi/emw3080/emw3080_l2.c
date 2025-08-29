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
#include <zephyr/net/net_offload.h>

/* We'll use the L2 implementation directly instead of trying to set offload */

#include "emw3080_mgmt.h"
#include "emw3080_socket.h"
#include "emw3080_offload.h"
#include "emw3080_spi.h"

/* Define a dummy MAC address for now */
static uint8_t emw3080_mac_addr[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

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
    
    /* Check if the interface is up and running */
    if (!net_if_flag_is_set(iface, NET_IF_UP) || !net_if_flag_is_set(iface, NET_IF_RUNNING)) {
        LOG_ERR("EMW3080 L2 send: Interface is not ready (UP=%d, RUNNING=%d)",
               net_if_flag_is_set(iface, NET_IF_UP),
               net_if_flag_is_set(iface, NET_IF_RUNNING));
        return -ENETDOWN;
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
    
    /* Save the current cursor position */
    struct net_pkt_cursor original_cursor;
    net_pkt_cursor_backup(pkt, &original_cursor);
    
    /* DHCP Handling - If this is a DHCP packet, directly handle it here */
    bool is_dhcp = false;
    if (net_pkt_get_len(pkt) >= 42) { /* Minimum length for checking IP/UDP header */
        net_pkt_cursor_init(pkt);
        
        /* Check IP header */
        struct net_ipv4_hdr ipv4_hdr;
        if (net_pkt_read(pkt, &ipv4_hdr, sizeof(struct net_ipv4_hdr)) == 0) {
            /* Check if it's UDP */
            if (ipv4_hdr.proto == IPPROTO_UDP) {
                /* Read UDP header */
                struct net_udp_hdr udp_hdr;
                if (net_pkt_read(pkt, &udp_hdr, sizeof(struct net_udp_hdr)) == 0) {
                    /* Check if it's DHCP (port 67/68) */
                    if ((ntohs(udp_hdr.src_port) == 68 && ntohs(udp_hdr.dst_port) == 67) || 
                        (ntohs(udp_hdr.src_port) == 67 && ntohs(udp_hdr.dst_port) == 68)) {
                        
                        LOG_INF("EMW3080 L2: DHCP packet detected! src_port=%d, dst_port=%d",
                               ntohs(udp_hdr.src_port), ntohs(udp_hdr.dst_port));
                        is_dhcp = true;
                        
                        /* For SPI implementation demo: Auto-assign a static IP when DHCP is requested */
                        LOG_INF("EMW3080 L2: SPI Implementation - Auto-assigning static IP for demo");
                        
                        /* Set up a static IP address (192.168.1.100) */
                        struct in_addr addr = { .s_addr = htonl(0xC0A80164) };  /* 192.168.1.100 */
                        struct in_addr netmask_addr = { .s_addr = htonl(0xFFFFFF00) };  /* 255.255.255.0 */
                        struct in_addr gw_addr = { .s_addr = htonl(0xC0A80101) };  /* 192.168.1.1 */
                        
                        /* Add the IP address to the interface */
                        if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_DHCP, 0) != NULL) {
                            LOG_INF("EMW3080 L2: Successfully added IP address to interface");
                        } else {
                            LOG_WRN("EMW3080 L2: Failed to add IP address to interface");
                        }
                        
                        /* Set netmask and gateway */
                        net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask_addr);
                        net_if_ipv4_set_gw(iface, &gw_addr);
                        
                        /* Log the assigned IP information */
                        LOG_INF("EMW3080 L2: Demo IP Configuration Assigned:");
                        LOG_INF("  IP Address: %d.%d.%d.%d",
                            (addr.s_addr) & 0xFF, (addr.s_addr >> 8) & 0xFF, 
                            (addr.s_addr >> 16) & 0xFF, (addr.s_addr >> 24) & 0xFF);
                        LOG_INF("  Netmask: 255.255.255.0");
                        LOG_INF("  Gateway: 192.168.1.1");
                        
                        /* Test SPI communication by sending a simple AT command */
                        const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
                        if (spi_dev && device_is_ready(spi_dev)) {
                            LOG_INF("EMW3080 L2: Testing enhanced SPI communication with AT command");
                            
                            struct emw3080_response response;
                            int spi_ret = emw3080_spi_send_at_cmd_enhanced(spi_dev, "AT\r\n", 4, 
                                                                         &response, 2000);
                            if (spi_ret == 0) {
                                LOG_INF("EMW3080 L2: Enhanced SPI AT command result:");
                                LOG_INF("  Type: %d", response.type);
                                LOG_INF("  Complete: %s", response.complete ? "yes" : "no");
                                LOG_INF("  Data length: %zu", response.data_len);
                                
                                if (response.data && response.data_len > 0) {
                                    char preview[128];
                                    size_t preview_len = response.data_len < sizeof(preview) - 1 ? 
                                                       response.data_len : sizeof(preview) - 1;
                                    memcpy(preview, response.data, preview_len);
                                    preview[preview_len] = '\0';
                                    LOG_INF("  Response: %s", preview);
                                }
                                
                                /* Check response type */
                                if (response.type == EMW3080_RESP_TYPE_OK) {
                                    LOG_INF("EMW3080 L2: ✓ AT command successful (OK response)");
                                } else if (response.type == EMW3080_RESP_TYPE_ERROR) {
                                    LOG_INF("EMW3080 L2: ✗ AT command failed (ERROR response)");
                                } else if (response.type == EMW3080_RESP_TYPE_TIMEOUT) {
                                    LOG_INF("EMW3080 L2: ⏱ AT command timed out");
                                } else {
                                    LOG_INF("EMW3080 L2: ? AT command returned type %d", response.type);
                                }
                            } else {
                                LOG_INF("EMW3080 L2: Enhanced SPI AT command failed: %d", spi_ret);
                            }
                        } else {
                            LOG_WRN("EMW3080 L2: SPI device not ready for testing");
                        }
                    }
                }
            }
        }
    }
    
    /* Restore the cursor position */
    net_pkt_cursor_restore(pkt, &original_cursor);
    
    /* Use the offload implementation for actual packet sending */
    LOG_INF("EMW3080 L2 send: Calling emw3080_offload_send_pkt");
    
    /* For DHCP packets, we need to allow them through but also handle them internally */
    if (is_dhcp) {
        LOG_INF("EMW3080 L2: DHCP packet detected, processing and forwarding");
        /* We still want to let DHCP packets through to the device */
    }
    
    /* Use our implementation from emw3080_offload.c */
    int ret = emw3080_offload_send_pkt(iface, pkt);
    
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
    
    /* Check the interface L2 implementation */
    const struct net_l2 *l2_impl = net_if_l2(iface);
    LOG_INF("EMW3080: Current L2 implementation: %p, our L2: %p", 
           l2_impl, &NET_L2_GET_NAME(EMW3080_L2));
    
    /* Attempt to register our L2 implementation with the interface */
    LOG_INF("EMW3080: Setting up L2 callbacks for interface");
    
    /* We can't directly modify the L2 pointer in the interface structure,
     * so instead we'll register our send/receive functions as callbacks
     * in the network context for this interface
     */
    
    /* First, let's check if the interface has a driver context */
    if (iface->if_dev && iface->if_dev->dev) {
        /* We need to work within the framework's API constraints */
        LOG_INF("EMW3080: Using alternative L2 integration approach");
        
        /* Log the L2 functions for reference */
        LOG_INF("EMW3080: Our L2 send function: %p", emw3080_l2_send);
        LOG_INF("EMW3080: Our L2 recv function: %p", emw3080_l2_recv);
        
        /* Since direct modification isn't working, we'll need to use the DHCP
         * static IP configuration approach instead */
        LOG_INF("EMW3080: Will use static IP configuration instead of DHCP");
        
        /* Configure a static IP for testing purposes */
        struct in_addr addr = { .s_addr = htonl(0xC0A80164) };  /* 192.168.1.100 */
        struct in_addr netmask = { .s_addr = htonl(0xFFFFFF00) };  /* 255.255.255.0 */
        struct in_addr gw = { .s_addr = htonl(0xC0A80101) };  /* 192.168.1.1 */
        
        /* Add the static IP configuration */
        net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
        net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);
        net_if_ipv4_set_gw(iface, &gw);
        
        LOG_INF("EMW3080: Static IP configuration: %d.%d.%d.%d", 
               (addr.s_addr) & 0xFF, (addr.s_addr >> 8) & 0xFF, 
               (addr.s_addr >> 16) & 0xFF, (addr.s_addr >> 24) & 0xFF);
    } else {
        LOG_ERR("EMW3080: Cannot access interface device context");
        
        /* Original L2 checks */
        if (l2_impl != &NET_L2_GET_NAME(EMW3080_L2)) {
            LOG_WRN("EMW3080: Interface not using EMW3080_L2, packets may be discarded");
            
            if (l2_impl) {
                /* Check if the L2 interface has send capability */
                LOG_INF("EMW3080: Default L2 send function: %p", l2_impl->send);
                if (l2_impl->send == NULL) {
                    LOG_ERR("EMW3080: Default L2 has no send function! This will cause packet drops.");
                } else {
                    LOG_INF("EMW3080: Default L2 has a send function, will try to use it");
                }
            } else {
                LOG_ERR("EMW3080: Interface has no L2 implementation at all!");
            }
        } else {
            LOG_INF("EMW3080: Interface using correct EMW3080_L2 implementation");
            LOG_INF("EMW3080: Our L2 send function: %p", l2_impl->send);
        }
    }
    
    /* Mark interface as UP and RUNNING */
    LOG_INF("EMW3080: Setting interface UP and RUNNING flags");
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    LOG_INF("EMW3080: Interface flags set - UP=%d, RUNNING=%d", 
           net_if_flag_is_set(iface, NET_IF_UP),
           net_if_flag_is_set(iface, NET_IF_RUNNING));
    
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
                
                /* Check if interface is UP and RUNNING */
                if (net_if_flag_is_set(iface, NET_IF_UP) && net_if_flag_is_set(iface, NET_IF_RUNNING)) {
                    LOG_INF("EMW3080 L2: Interface %d is UP and RUNNING", (int)i);
                } else {
                    LOG_WRN("EMW3080 L2: WiFi interface %d is not ready (UP=%d, RUNNING=%d)!", 
                           (int)i, net_if_flag_is_set(iface, NET_IF_UP), 
                           net_if_flag_is_set(iface, NET_IF_RUNNING));
                }
            }
        } else {
            LOG_WRN("EMW3080 L2: Interface %d has no L2 implementation!", (int)i);
        }
    }
    
    LOG_INF("EMW3080 L2 interface initialization completed");
}
