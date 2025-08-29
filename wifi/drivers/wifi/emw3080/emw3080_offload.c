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
#include <zephyr/net/net_l2.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/offloaded_netdev.h>
#include "emw3080_dhcp.h"
#include "emw3080_l2.h"

/* Include the L2 implementation for offloaded network devices */
NET_L2_DECLARE_PUBLIC(OFFLOADED_NETDEV);

/* We'll use the L2 implementation directly instead of offload API */

/* Ethernet L2 compatible send function that can be called directly by L2 */
int emw3080_offload_send_pkt(struct net_if *iface, struct net_pkt *pkt)
{
    LOG_INF("EMW3080 offload_send_pkt called: iface=%p (idx=%d), pkt=%p, len=%d", 
           iface, net_if_get_by_iface(iface), pkt, net_pkt_get_len(pkt));
           
    if (!iface || !pkt) {
        LOG_ERR("EMW3080 offload_send_pkt: Invalid parameters: iface=%p, pkt=%p", iface, pkt);
        return -EINVAL;
    }
    
    /* Check if the interface is up and running */
    if (!net_if_flag_is_set(iface, NET_IF_UP) || !net_if_flag_is_set(iface, NET_IF_RUNNING)) {
        LOG_ERR("EMW3080 offload_send_pkt: Interface is not ready (UP=%d, RUNNING=%d)",
              net_if_flag_is_set(iface, NET_IF_UP),
              net_if_flag_is_set(iface, NET_IF_RUNNING));
        return -ENETDOWN;
    }
    
    /* Get device context */
    const struct device *dev = net_if_get_device(iface);
    if (!dev) {
        LOG_ERR("EMW3080 offload_send_pkt: No device associated with interface");
        return -ENODEV;
    }
    
    /* Backup the current cursor position */
    struct net_pkt_cursor backup;
    net_pkt_cursor_backup(pkt, &backup);
    
    /* Check packet contents */
    char *proto_name = "Unknown";
    struct net_ipv4_hdr ipv4_hdr;
    uint8_t next_proto = 0;
    bool is_dhcp = false;
    
    /* Try to access IPv4 header */
    net_pkt_cursor_init(pkt);
    
    if (net_pkt_read(pkt, &ipv4_hdr, sizeof(struct net_ipv4_hdr)) < 0) {
        LOG_ERR("EMW3080 offload_send_pkt: Failed to read IPv4 header");
        net_pkt_cursor_restore(pkt, &backup);
        return -EINVAL;
    }
    
    /* Check if this is a valid IPv4 packet */
    if ((ipv4_hdr.vhl >> 4) == 4) {  // IPv4 version
        next_proto = ipv4_hdr.proto;
        
        /* Check for UDP protocol */
        if (next_proto == IPPROTO_UDP) {
            proto_name = "UDP";
            
            /* Read UDP header */
            struct net_udp_hdr udp_hdr;
            if (net_pkt_read(pkt, &udp_hdr, sizeof(struct net_udp_hdr)) < 0) {
                LOG_ERR("EMW3080 offload_send_pkt: Failed to read UDP header");
                net_pkt_cursor_restore(pkt, &backup);
                return -EINVAL;
            }
            
            /* Check if it's DHCP (port 67/68) */
            uint16_t src_port = ntohs(udp_hdr.src_port);
            uint16_t dst_port = ntohs(udp_hdr.dst_port);
            
            if ((src_port == 68 && dst_port == 67) || (src_port == 67 && dst_port == 68)) {
                LOG_INF("DHCP packet detected - ports %d -> %d", src_port, dst_port);
                is_dhcp = true;
                
                /* For DHCP packets in SPI implementation, we return success immediately */
                /* The actual IP configuration is handled in the L2 layer */
                LOG_INF("EMW3080 offload_send_pkt: DHCP packet - returning success (SPI implementation)");
                net_pkt_cursor_restore(pkt, &backup);
                return net_pkt_get_len(pkt);  /* Return packet size as successfully sent */
            }
            
        } else if (next_proto == IPPROTO_ICMP) {
            proto_name = "ICMP";
        } else if (next_proto == IPPROTO_TCP) {
            proto_name = "TCP";
        }
        
        /* Log packet details */
        LOG_INF("Sending %s packet from %d.%d.%d.%d to %d.%d.%d.%d",
              proto_name,
              ipv4_hdr.src[0], ipv4_hdr.src[1], ipv4_hdr.src[2], ipv4_hdr.src[3],
              ipv4_hdr.dst[0], ipv4_hdr.dst[1], ipv4_hdr.dst[2], ipv4_hdr.dst[3]);
    } else {
        LOG_ERR("EMW3080 offload_send_pkt: Not an IPv4 packet (version=%d)", 
              ipv4_hdr.vhl >> 4);
        net_pkt_cursor_restore(pkt, &backup);
        return -EPROTONOSUPPORT;
    }
    
    /* Restore the cursor */
    net_pkt_cursor_restore(pkt, &backup);
    
    /* Handle non-DHCP packets by delegating to socket implementation */
    if (!is_dhcp) {
        LOG_INF("EMW3080 offload_send_pkt: Sending packet via socket API");
        
        /* Use the emw3080_send_pkt function from socket.c to send the packet */
        extern int emw3080_send_pkt(struct net_if *iface, struct net_pkt *pkt);
        int ret = emw3080_send_pkt(iface, pkt);
        
        if (ret < 0) {
            LOG_ERR("EMW3080 offload_send_pkt: Failed to send packet: %d", ret);
            return ret;
        }
        
        LOG_INF("EMW3080 offload_send_pkt: Successfully sent %d bytes", ret);
        return ret;
    }
    
    /* DHCP packets are handled in the L2 layer */
    LOG_INF("EMW3080 offload_send_pkt: DHCP packet handled");
    return net_pkt_get_len(pkt);  /* Return full packet length as success */
}

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
    
    /* Get the interface from the packet */
    struct net_if *iface = net_pkt_iface(pkt);
    if (!iface) {
        LOG_ERR("No interface associated with packet");
        return -EINVAL;
    }
    
    /* Use the Ethernet L2 send method directly if available */
    const struct ethernet_api *eth_api = NULL;
    const struct device *dev = net_if_get_device(iface);
    
    if (dev && dev->api) {
        eth_api = (const struct ethernet_api *)dev->api;
        if (eth_api && eth_api->send) {
            LOG_INF("Using Ethernet L2 API send function");
            /* Forward to the Ethernet send function */
            int ret = emw3080_offload_send_pkt(iface, pkt);
            
            /* Call the callback with success/failure */
            if (cb) {
                cb(user_data, ret, pkt);
            }
            
            return ret;
        }
    }
    
    /* Fallback to our own send implementation if L2 isn't available */
    LOG_INF("Using offload send function - L2 API not available");
    int ret = emw3080_offload_send_pkt(iface, pkt);
    
    /* Call the callback with success/failure */
    if (cb) {
        cb(user_data, ret, pkt);
    }
    
    return ret;
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
