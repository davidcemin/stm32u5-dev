/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_dhcp, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_ip.h>

/* Function to handle DHCP packets specifically */
int emw3080_handle_dhcp(struct net_if *iface, struct net_pkt *pkt)
{
    LOG_INF("EMW3080 handling DHCP packet");
    
    /* In a real implementation, this would process the DHCP packet and configure 
     * the interface with the obtained IP address. For now, we'll simulate this by
     * assigning a static IP.
     */
    
    /* Create a static IPv4 configuration */
    struct in_addr addr;
    struct in_addr netmask;
    struct in_addr gw;
    
    /* Set up a mock IP address (192.168.1.100) */
    addr.s_addr = htonl(0xC0A80164);  /* 192.168.1.100 */
    netmask.s_addr = htonl(0xFFFFFF00);  /* 255.255.255.0 */
    gw.s_addr = htonl(0xC0A80101);  /* 192.168.1.1 */
    
    /* Add the IP address to the interface */
    net_if_ipv4_addr_add(iface, &addr, NET_ADDR_DHCP, 0);
    
    /* Set netmask */
    struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
    if (ipv4) {
        net_if_ipv4_set_netmask(ipv4, &netmask);
    }
    
    /* Set gateway */
    net_if_ipv4_set_gw(iface, &gw);
    
    /* Log the assigned IP information */
    LOG_INF("EMW3080 DHCP: IP=%d.%d.%d.%d, Mask=%d.%d.%d.%d, GW=%d.%d.%d.%d",
           (addr.s_addr >> 0) & 0xFF, (addr.s_addr >> 8) & 0xFF, 
           (addr.s_addr >> 16) & 0xFF, (addr.s_addr >> 24) & 0xFF,
           (netmask.s_addr >> 0) & 0xFF, (netmask.s_addr >> 8) & 0xFF,
           (netmask.s_addr >> 16) & 0xFF, (netmask.s_addr >> 24) & 0xFF,
           (gw.s_addr >> 0) & 0xFF, (gw.s_addr >> 8) & 0xFF,
           (gw.s_addr >> 16) & 0xFF, (gw.s_addr >> 24) & 0xFF);
    
    return 0;
}

/* Function to check if a packet is DHCP */
bool emw3080_is_dhcp_packet(struct net_pkt *pkt)
{
    /* Check if we have enough data for IP + UDP headers */
    if (net_pkt_get_len(pkt) < (sizeof(struct net_ipv4_hdr) + sizeof(struct net_udp_hdr))) {
        return false;
    }
    
    /* Access headers */
    struct net_ipv4_hdr *ipv4_hdr = NET_IPV4_HDR(pkt);
    if (!ipv4_hdr || ipv4_hdr->proto != IPPROTO_UDP) {
        return false;
    }
    
    /* Check if it's UDP */
    struct net_udp_hdr *udp_hdr = (struct net_udp_hdr *)((uint8_t *)ipv4_hdr + sizeof(struct net_ipv4_hdr));
    if (!udp_hdr) {
        return false;
    }
    
    /* Check source and destination ports for DHCP */
    if ((ntohs(udp_hdr->src_port) == 68 && ntohs(udp_hdr->dst_port) == 67) || 
        (ntohs(udp_hdr->src_port) == 67 && ntohs(udp_hdr->dst_port) == 68)) {
        LOG_INF("Identified DHCP packet - ports %d -> %d", 
               ntohs(udp_hdr->src_port), ntohs(udp_hdr->dst_port));
        return true;
    }
    
    return false;
}
