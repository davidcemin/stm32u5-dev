/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(emw3080, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/socket_offload.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>
#include "emw3080_dhcp.h"

/* Network offload operations */
static int emw3080_get(sa_family_t family, enum net_sock_type type,
                      enum net_ip_protocol ip_proto,
                      struct net_context **context)
{
    LOG_INF("EMW3080 net_offload get operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_bind(struct net_context *context,
                       const struct sockaddr *addr,
                       socklen_t addrlen)
{
    LOG_INF("EMW3080 net_offload bind operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_listen(struct net_context *context, int backlog)
{
    LOG_INF("EMW3080 net_offload listen operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_connect(struct net_context *context,
                          const struct sockaddr *addr,
                          socklen_t addrlen,
                          net_context_connect_cb_t cb,
                          int32_t timeout,
                          void *user_data)
{
    LOG_INF("EMW3080 net_offload connect operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_accept(struct net_context *context,
                         net_tcp_accept_cb_t cb,
                         int32_t timeout,
                         void *user_data)
{
    LOG_INF("EMW3080 net_offload accept operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_send(struct net_pkt *pkt,
                       net_context_send_cb_t cb,
                       int32_t timeout,
                       void *user_data)
{
    LOG_INF("EMW3080 net_offload send operation - packet size: %d bytes", 
           net_pkt_get_len(pkt));
    
    /* Check if this is a DHCP or UDP packet - needs special handling */
    /* Extract packet info for debugging */
    char *proto_name = "Unknown";
    struct net_ipv4_hdr *ipv4_hdr = NULL;
    uint8_t next_proto = 0;
    bool is_dhcp = false;
    
    /* Try to access IPv4 header */
    if (net_pkt_get_len(pkt) >= sizeof(struct net_ipv4_hdr)) {
        ipv4_hdr = NET_IPV4_HDR(pkt);
        if (ipv4_hdr) {
            next_proto = ipv4_hdr->proto;
            
            /* Check for UDP protocol */
            if (next_proto == IPPROTO_UDP) {
                proto_name = "UDP";
                
                /* Check for DHCP ports */
                struct net_udp_hdr *udp_hdr = (struct net_udp_hdr *)((uint8_t *)ipv4_hdr + sizeof(struct net_ipv4_hdr));
                if (udp_hdr) {
                    /* Check if it's DHCP (port 67/68) */
                    if ((ntohs(udp_hdr->src_port) == 68 && ntohs(udp_hdr->dst_port) == 67) || 
                        (ntohs(udp_hdr->src_port) == 67 && ntohs(udp_hdr->dst_port) == 68)) {
                        LOG_INF("DHCP packet detected - ports %d -> %d", 
                            ntohs(udp_hdr->src_port), ntohs(udp_hdr->dst_port));
                        is_dhcp = true;
                        
                        /* Get the interface from the packet */
                        struct net_if *iface = net_pkt_iface(pkt);
                        if (!iface) {
                            LOG_ERR("No interface associated with packet");
                            return -EINVAL;
                        }
                        
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
                            
                        /* Call the callback with success */
                        if (cb) {
                            cb(user_data, 0, pkt);
                        }
                        
                        return 0;
                    }
                }
            } else if (next_proto == IPPROTO_ICMP) {
                proto_name = "ICMP";
            } else if (next_proto == IPPROTO_TCP) {
                proto_name = "TCP";
            } else {
                proto_name = "Other";
            }
            
            /* Log packet details */
            LOG_INF("Sending %s packet from %d.%d.%d.%d to %d.%d.%d.%d",
                proto_name,
                ipv4_hdr->src[0], ipv4_hdr->src[1], ipv4_hdr->src[2], ipv4_hdr->src[3],
                ipv4_hdr->dst[0], ipv4_hdr->dst[1], ipv4_hdr->dst[2], ipv4_hdr->dst[3]);
        }
    }
    
    /* We've already processed packet info in the first half of the function */
    
    /* In a real implementation, we would transmit the packet through the WiFi module */
    /* For now, we'll pretend the packet was sent successfully */
    
    if (cb) {
        cb(user_data, 0, pkt);  /* Call the callback indicating success */
    }
    
    return 0;  /* Success */
}

static int emw3080_sendto(struct net_pkt *pkt,
                         const struct sockaddr *dst_addr,
                         socklen_t addrlen,
                         net_context_send_cb_t cb,
                         int32_t timeout,
                         void *user_data)
{
    LOG_INF("EMW3080 net_offload sendto operation - packet size: %d bytes", 
           net_pkt_get_len(pkt));
    
    /* In a real implementation, we would transmit the packet through the WiFi module */
    /* For DHCP and common protocols, we can delegate to our send implementation */
    return emw3080_send(pkt, cb, timeout, user_data);
}

static int emw3080_recv(struct net_context *context,
                       net_context_recv_cb_t cb,
                       int32_t timeout,
                       void *user_data)
{
    LOG_INF("EMW3080 net_offload recv operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_put(struct net_context *context)
{
    LOG_INF("EMW3080 net_offload put operation (not yet implemented)");
    return -ENOTSUP;
}

/* Define the offload API */
const struct net_offload emw3080_offload = {
    .get = emw3080_get,
    .bind = emw3080_bind,
    .listen = emw3080_listen,
    .connect = emw3080_connect,
    .accept = emw3080_accept,
    .send = emw3080_send,
    .sendto = emw3080_sendto,
    .recv = emw3080_recv,
    .put = emw3080_put,
};
