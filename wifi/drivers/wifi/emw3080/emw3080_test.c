/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wifi_test, CONFIG_LOG_DEFAULT_LEVEL);

#include "emw3080.h"
#include "emw3080_socket.h"

/* Forward declaration - defined in emw3080_net.c */
extern const struct device *get_emw3080_net_device(void);

/* SSID and PSK to use for connecting */
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PSK "YourWiFiPassword"

/* Test AT command functionality */
int emw3080_test_at_commands(void)
{
    LOG_INF("Testing EMW3080 AT commands");
    
    /* Get the device instance */
    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("EMW3080 device not found");
        return -ENODEV;
    }
    
    struct emw3080_data *data = dev->data;
    if (!data) {
        LOG_ERR("EMW3080 device data not found");
        return -EINVAL;
    }
    
    /* Test simple AT command */
    char resp[128];
    LOG_INF("Testing basic AT command");
    /* Increase timeout to 5000ms (5 seconds) */
    int ret = emw3080_send_at_cmd(data, "AT\r\n", 4, resp, sizeof(resp), 5000);
    if (ret < 0) {
        LOG_ERR("Failed to send AT command: %d (%s)", ret, strerror(-ret));
        return ret;
    }
    LOG_INF("AT command response: %s", resp);
    
    /* Test multi-connection mode */
    LOG_INF("Testing multi-connection mode");
    char cmd[32];
    snprintf(cmd, sizeof(cmd), emw3080_cmd_set_multi_conn, 1);
    /* Increase timeout to 5000ms (5 seconds) */
    ret = emw3080_send_at_cmd(data, cmd, strlen(cmd), resp, sizeof(resp), 5000);
    if (ret < 0) {
        LOG_ERR("Failed to set multi-connection mode: %d (%s)", ret, strerror(-ret));
        return ret;
    }
    LOG_INF("Multi-connection mode response: %s", resp);
    
    return 0;
}

/* Function to handle WiFi events */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                  uint64_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        LOG_INF("WiFi connected!");
        
        /* Start DHCP after successful connection */
        LOG_INF("Starting DHCPv4 client...");
        net_dhcpv4_start(iface);
        break;
        
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("WiFi disconnected!");
        break;
        
    case NET_EVENT_IPV4_ADDR_ADD:
        LOG_INF("IPv4 address assigned!");
        break;
        
    default:
        LOG_DBG("Unhandled WiFi event: 0x%016llx", mgmt_event);
        break;
    }
}

void test_wifi_l2_init(void)
{
    struct net_mgmt_event_callback wifi_cb;
    const struct device *wifi_dev;
    struct net_if *iface;
    int ret;
    
    /* Initialize event callback */
    /* Use a cast to handle the uint32_t vs uint64_t type difference in newer Zephyr */
    net_mgmt_init_event_callback(&wifi_cb, 
                                (net_mgmt_event_handler_t)wifi_mgmt_event_handler,
                                NET_EVENT_WIFI_CONNECT_RESULT |
                                NET_EVENT_WIFI_DISCONNECT_RESULT |
                                NET_EVENT_IPV4_ADDR_ADD);
    
    net_mgmt_add_event_callback(&wifi_cb);
    
    /* Get the WiFi device */
    wifi_dev = get_emw3080_net_device();
    if (!wifi_dev) {
        LOG_ERR("Could not get WiFi device");
        return;
    }
    
    /* Get the interface for this device */
    iface = NULL;
    for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp_if = net_if_get_by_index(i);
        if (!tmp_if) {
            continue;
        }
        
        const struct device *tmp_dev = net_if_get_device(tmp_if);
        if (tmp_dev == wifi_dev) {
            iface = tmp_if;
            break;
        }
    }
    
    if (!iface) {
        LOG_ERR("Could not find interface for WiFi device");
        return;
    }
    
    LOG_INF("Found WiFi interface at index %d", net_if_get_by_iface(iface));
    
    /* Connect to WiFi network */
    struct wifi_connect_req_params wifi_params = {
        .ssid = WIFI_SSID,
        .ssid_length = sizeof(WIFI_SSID) - 1,
        .psk = WIFI_PSK,
        .psk_length = sizeof(WIFI_PSK) - 1,
        .channel = WIFI_CHANNEL_ANY,
        .security = WIFI_SECURITY_TYPE_PSK,
    };
    
    LOG_INF("Connecting to SSID: %s", wifi_params.ssid);
    
    ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wifi_params, sizeof(wifi_params));
    if (ret) {
        LOG_ERR("Failed to connect to WiFi network: %d", ret);
        return;
    }
    
    LOG_INF("WiFi connection request sent");
}
