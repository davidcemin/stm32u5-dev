/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_socket, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_ip.h>
#include <string.h>

#include "emw3080.h"
#include "emw3080_socket.h"

/* Forward declarations from other files */
extern int emw3080_send_at_cmd(struct emw3080_data *data, 
                              const char *cmd, size_t cmd_len,
                              char *resp_buf, size_t resp_len,
                              uint32_t timeout_ms);

/* Convert IPv4 address to string format */
static void ipv4_addr_to_str(const uint8_t *addr, char *str, size_t size)
{
    snprintf(str, size, "%d.%d.%d.%d", addr[0], addr[1], addr[2], addr[3]);
}

/* Check if a packet is DHCP */
static bool is_dhcp_packet(struct net_pkt *pkt)
{
    struct net_ipv4_hdr ip_hdr;
    struct net_udp_hdr udp_hdr;
    
    /* Save packet cursor and set to beginning */
    struct net_pkt_cursor backup_cursor;
    net_pkt_cursor_backup(pkt, &backup_cursor);
    net_pkt_cursor_init(pkt);
    
    /* Read IP header */
    if (net_pkt_read(pkt, &ip_hdr, sizeof(struct net_ipv4_hdr)) < 0) {
        net_pkt_cursor_restore(pkt, &backup_cursor);
        return false;
    }
    
    /* If not UDP, not DHCP */
    if (ip_hdr.proto != IPPROTO_UDP) {
        net_pkt_cursor_restore(pkt, &backup_cursor);
        return false;
    }
    
    /* Read UDP header */
    if (net_pkt_read(pkt, &udp_hdr, sizeof(struct net_udp_hdr)) < 0) {
        net_pkt_cursor_restore(pkt, &backup_cursor);
        return false;
    }
    
    /* Restore cursor position */
    net_pkt_cursor_restore(pkt, &backup_cursor);
    
    /* Check for DHCP ports (67/68) */
    if ((ntohs(udp_hdr.src_port) == 68 && ntohs(udp_hdr.dst_port) == 67) || 
        (ntohs(udp_hdr.src_port) == 67 && ntohs(udp_hdr.dst_port) == 68)) {
        return true;
    }
    
    return false;
}

/* Find a free socket */
static int emw3080_get_free_socket(struct emw3080_data *data)
{
    for (int i = 0; i < EMW3080_MAX_CONNECTIONS; i++) {
        if (!data->sockets[i].in_use) {
            return i;
        }
    }
    return -1;  /* No free sockets */
}

/* This function is called when a packet needs to be sent */
int emw3080_send_pkt(struct net_if *iface, struct net_pkt *pkt)
{
    LOG_INF("EMW3080 send packet: iface=%p, pkt=%p, len=%d", 
           iface, pkt, net_pkt_get_len(pkt));
    
    /* For now, just return success for DHCP packets since they're handled at the L2 level */
    bool is_dhcp = is_dhcp_packet(pkt);
    if (is_dhcp) {
        LOG_INF("DHCP packet in send_pkt - returning success (handled at L2)");
        return net_pkt_get_len(pkt);
    }
    
    /* Get the SPI device directly since EMW3080_NET device might not have the full data structure */
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
    if (!spi_dev || !device_is_ready(spi_dev)) {
        LOG_ERR("SPI2 device not available for packet transmission");
        return -ENODEV;
    }
    
    LOG_INF("EMW3080 send packet via SPI: Using SPI device %s", spi_dev->name);
    
    /* For other packets, we'll need to implement actual packet transmission via SPI */
    /* For now, let's just log and return success to avoid blocking */
    LOG_INF("Non-DHCP packet send via SPI - TODO: implement actual transmission");
    return net_pkt_get_len(pkt);
}
    
/* Process received data from AT command responses */
void emw3080_process_ipd(struct emw3080_data *data, const uint8_t *ipd_data, uint16_t len)
{
    /* This parses +IPD messages and creates net_pkt to send up the stack */
    /* Example format: +IPD,<conn_id>,<len>:<data> */
    
    int conn_id = 0;
    int data_len = 0;
    const uint8_t *payload = NULL;
    
    /* Print the first bytes of raw data for debugging */
    LOG_DBG("Raw IPD data (%d bytes): '%.*s'", len, len > 30 ? 30 : len, ipd_data);
    
    /* Ensure we have a null-terminated string for parsing */
    char ipd_str[32];
    size_t copy_len = len > 31 ? 31 : len;
    memcpy(ipd_str, ipd_data, copy_len);
    ipd_str[copy_len] = '\0';
    
    /* Parse the +IPD message to extract connection ID and length */
    if (sscanf(ipd_str, "+IPD,%d,%d:", &conn_id, &data_len) != 2) {
        LOG_ERR("Invalid IPD format: '%s'", ipd_str);
        
        /* Try to recover - search for +IPD pattern */
        char *ipd_pattern = strstr((const char *)ipd_data, "+IPD,");
        if (ipd_pattern && sscanf(ipd_pattern, "+IPD,%d,%d:", &conn_id, &data_len) == 2) {
            LOG_INF("Recovered IPD pattern: conn=%d, len=%d", conn_id, data_len);
        } else {
            LOG_ERR("Failed to recover IPD pattern");
            return;
        }
    }
    
    /* Find payload start - after the colon */
    const char *colon = strchr((const char *)ipd_data, ':');
    if (!colon) {
        LOG_ERR("No payload delimiter (:) in IPD message");
        return;
    }
    
    payload = (const uint8_t *)(colon + 1);  /* Skip the colon */
    
    /* Calculate actual payload length by counting remaining data */
    int actual_payload_len = len - (payload - ipd_data);
    
    LOG_INF("Processing IPD data: conn=%d, reported_len=%d, actual_len=%d", 
           conn_id, data_len, actual_payload_len);
    
    /* Validate lengths */
    if (actual_payload_len < data_len) {
        LOG_WRN("Incomplete data: expected %d bytes, got %d bytes", 
               data_len, actual_payload_len);
        data_len = actual_payload_len; /* Use the available data */
    } else if (actual_payload_len > data_len) {
        LOG_WRN("Extra data: expected %d bytes, got %d bytes", 
               data_len, actual_payload_len);
        /* Continue with reported length */
    }
    
    /* Check if we have a valid connection */
    if (conn_id < 0 || conn_id >= EMW3080_MAX_CONNECTIONS) {
        LOG_ERR("Invalid connection ID: %d", conn_id);
        return;
    }
    
    /* Get socket information */
    struct emw3080_socket *socket = &data->sockets[conn_id];
    if (!socket->in_use) {
        LOG_WRN("Received data for inactive socket %d", conn_id);
        /* We'll continue processing anyway - the socket might have been closed
         * on our side but data could still be arriving */
    }
    
    /* Ensure we have data to process */
    if (data_len <= 0) {
        LOG_WRN("Zero or negative data length (%d), skipping packet creation", data_len);
        return;
    }
    
    /* Create a new packet with headers */
    struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(data->iface, data_len, 
                                                     AF_INET, socket->proto, 
                                                     K_MSEC(100));
    if (!pkt) {
        LOG_ERR("Failed to allocate packet for %d bytes", data_len);
        return;
    }
    
    /* Copy the data into the packet */
    if (net_pkt_write(pkt, payload, data_len) < 0) {
        LOG_ERR("Failed to write payload to packet");
        net_pkt_unref(pkt);
        return;
    }
    
    /* Reset cursor for reading by upper layers */
    net_pkt_cursor_init(pkt);
    
    /* Check if this might be a DHCP response - UDP port 67/68 */
    if (data_len >= 240) { /* Minimum DHCP packet size */
        /* Check for DHCP magic cookie */
        const uint8_t *dhcp_cookie = payload + 236;
        if (data_len > 240 && 
            dhcp_cookie[0] == 0x63 && dhcp_cookie[1] == 0x82 && 
            dhcp_cookie[2] == 0x53 && dhcp_cookie[3] == 0x63) {
            LOG_INF("DHCP response detected (magic cookie verified)");
            
            /* Handle DHCP response - extract IP information */
            struct in_addr addr = {0};
            struct in_addr netmask = {0};
            struct in_addr gateway = {0};
            
            /* Extract IP from DHCP packet - yiaddr field at offset 16 (4 bytes) */
            memcpy(&addr.s_addr, payload + 16, 4);
            
            /* Set static configuration for testing */
            if (addr.s_addr == 0) {
                /* Use fallback static address if DHCP doesn't provide one */
                addr.s_addr = htonl(0xC0A80164);     /* 192.168.1.100 */
                netmask.s_addr = htonl(0xFFFFFF00);  /* 255.255.255.0 */
                gateway.s_addr = htonl(0xC0A80101);  /* 192.168.1.1 */
            } else {
                /* Use standard class C netmask and gateway on same subnet */
                netmask.s_addr = htonl(0xFFFFFF00);  /* 255.255.255.0 */
                gateway.s_addr = addr.s_addr & netmask.s_addr;
                gateway.s_addr |= htonl(0x01);       /* .1 for gateway */
            }
            
            /* Apply network configuration */
            LOG_INF("DHCP: Configuring IP=%d.%d.%d.%d", 
                  (addr.s_addr) & 0xFF, (addr.s_addr >> 8) & 0xFF, 
                  (addr.s_addr >> 16) & 0xFF, (addr.s_addr >> 24) & 0xFF);
            
            /* Add the IP address to the interface */
            net_if_ipv4_addr_add(data->iface, &addr, NET_ADDR_DHCP, 0);
            
            /* Set netmask and gateway */
            net_if_ipv4_set_netmask_by_addr(data->iface, &addr, &netmask);
            net_if_ipv4_set_gw(data->iface, &gateway);
        }
    }
    
    /* Send to upper layers */
    LOG_INF("Delivering %d bytes to network stack (iface=%p)", 
           data_len, data->iface);
    
    if (net_recv_data(data->iface, pkt) < 0) {
        LOG_ERR("Failed to deliver received data to stack");
        net_pkt_unref(pkt);
    } else {
        LOG_INF("Data successfully delivered to network stack");
    }
}
