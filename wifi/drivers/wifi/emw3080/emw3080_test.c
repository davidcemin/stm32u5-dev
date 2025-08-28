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

/* Forward declaration - defined in emw3080_net.c */
extern const struct device *get_emw3080_net_device(void);

/* SSID and PSK to use for connecting */
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PSK "YourWiFiPassword"

/* Function to handle WiFi events */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                  uint32_t mgmt_event, struct net_if *iface)
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
        LOG_DBG("Unhandled WiFi event: 0x%08x", mgmt_event);
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
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
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
