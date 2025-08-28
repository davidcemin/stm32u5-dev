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
    
    /* Configure the UART first */
    LOG_INF("Configuring UART for AT commands");
    extern int emw3080_configure_uart(const struct device *uart_dev);
    int ret = emw3080_configure_uart(data->uart);
    if (ret < 0) {
        LOG_ERR("Failed to configure UART: %d", ret);
        return ret;
    }
    
    /* Flush UART before starting tests */
    extern void emw3080_uart_flush_rx(const struct device *uart_dev);
    emw3080_uart_flush_rx(data->uart);
    
    /* Test with progressively longer timeouts */
    int timeouts[] = {1000, 2000, 5000};
    int i;
    bool success = false;
    
    /* Basic AT test */
    char resp[256];
    for (i = 0; i < ARRAY_SIZE(timeouts) && !success; i++) {
        int timeout = timeouts[i];
        LOG_INF("Testing basic AT command with timeout %d ms", timeout);
        
        memset(resp, 0, sizeof(resp));
        ret = emw3080_send_at_cmd(data, "AT\r\n", 4, resp, sizeof(resp), timeout);
        if (ret == 0) {
            LOG_INF("AT command successful! Response: %s", resp);
            success = true;
            break;
        } else {
            LOG_WRN("AT command failed with timeout %d ms: %d", timeout, ret);
        }
        
        /* Short pause between attempts */
        k_sleep(K_MSEC(100));
    }
    
    if (!success) {
        LOG_ERR("Failed to execute basic AT command after multiple attempts");
        LOG_ERR("This indicates a communication problem with the EMW3080 module");
        LOG_ERR("Check hardware connections and reset sequence");
        return -EIO;
    }
    
    /* Reset the success flag for next test */
    success = false;
    
    /* Test version command */
    LOG_INF("Testing AT version command");
    for (i = 0; i < ARRAY_SIZE(timeouts) && !success; i++) {
        int timeout = timeouts[i];
        
        memset(resp, 0, sizeof(resp));
        ret = emw3080_send_at_cmd(data, "AT+GMR\r\n", 8, resp, sizeof(resp), timeout);
        if (ret == 0) {
            LOG_INF("Version command successful! Response: %s", resp);
            success = true;
            break;
        } else {
            LOG_WRN("Version command failed with timeout %d ms: %d", timeout, ret);
        }
        
        /* Short pause between attempts */
        k_sleep(K_MSEC(100));
    }
    
    /* Test mode setting command */
    LOG_INF("Testing mode setting command");
    const char *mode_cmd = "AT+CWMODE=1\r\n"; /* Station mode */
    ret = emw3080_send_at_cmd(data, mode_cmd, strlen(mode_cmd), resp, sizeof(resp), 5000);
    if (ret < 0) {
        LOG_WRN("Failed to set mode: %d, but continuing with tests", ret);
    } else {
        LOG_INF("Mode setting successful! Response: %s", resp);
    }
    
    /* Test multi-connection mode */
    LOG_INF("Testing multi-connection mode");
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPMUX=1\r\n");
    ret = emw3080_send_at_cmd(data, cmd, strlen(cmd), resp, sizeof(resp), 5000);
    if (ret < 0) {
        LOG_WRN("Failed to set multi-connection mode: %d", ret);
    } else {
        LOG_INF("Multi-connection mode response: %s", resp);
    }
    
    /* Test getting the IP address */
    LOG_INF("Testing IP address query");
    ret = emw3080_send_at_cmd(data, "AT+CIFSR\r\n", 10, resp, sizeof(resp), 5000);
    if (ret < 0) {
        LOG_WRN("Failed to query IP: %d", ret);
    } else {
        LOG_INF("IP address query response: %s", resp);
    }
    
    LOG_INF("AT command testing complete");
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
