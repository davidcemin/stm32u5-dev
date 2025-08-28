/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_mgmt, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include "emw3080_mgmt.h"

/* This is a bridge module that connects the EMW3080 driver to the WiFi management API */

/* Forward declarations */
extern const struct device *get_emw3080_net_device(void);

/* Buffer to store scan results */
/* Define our own maximum scan result count */
#define EMW3080_MAX_SCAN_RESULTS 10
static struct wifi_scan_result scan_results[EMW3080_MAX_SCAN_RESULTS];
static int scan_result_count = 0;
static scan_result_cb_t active_scan_cb = NULL;
static struct net_if *scan_iface = NULL;

/* Define a mock MAC address for now */
static uint8_t mock_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

/* WiFi status information */
static struct wifi_iface_status current_status = {
    .state = WIFI_STATE_DISCONNECTED,
    .band = WIFI_FREQ_BAND_2_4_GHZ,
    .channel = 1,
    .security = WIFI_SECURITY_TYPE_NONE,
    .rssi = -90,
    .iface_mode = WIFI_STA_MODE,
    .mfp = WIFI_MFP_DISABLE
};

/* Work item to deliver scan results */
static void deliver_scan_results_handler(struct k_work *work)
{
    if (!active_scan_cb || !scan_iface) {
        LOG_ERR("No active scan callback or interface");
        return;
    }
    
    /* Deliver each scan result */
    for (int i = 0; i < scan_result_count; i++) {
        active_scan_cb(scan_iface, 0, &scan_results[i]);
        k_sleep(K_MSEC(10));  /* Small delay between results */
    }
    
    /* Signal the end of the scan */
    active_scan_cb(scan_iface, 0, NULL);
    
    /* Clear the active callback */
    active_scan_cb = NULL;
    scan_iface = NULL;
}

/* Initialize the work item */
K_WORK_DELAYABLE_DEFINE(deliver_scan_result_work, deliver_scan_results_handler);

/* Function to perform a mock WiFi scan */
int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb)
{
    LOG_INF("EMW3080 mock WiFi scan initiated");
    
    /* Store callback for use when delivering results */
    active_scan_cb = cb;
    
    /* Find the network interface for this device */
    for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *iface = net_if_get_by_index(i);
        if (!iface) {
            continue;
        }
        
        const struct device *iface_dev = net_if_get_device(iface);
        if (iface_dev == dev) {
            scan_iface = iface;
            break;
        }
    }
    
    if (!scan_iface) {
        LOG_ERR("Could not find interface for scan");
        return -ENODEV;
    }
    
    /* For testing, generate some mock scan results */
    scan_result_count = 3;  /* Return 3 mock networks */
    
    /* Mock network 1 */
    strcpy(scan_results[0].ssid, "EMW3080_NETWORK");
    scan_results[0].ssid_length = strlen("EMW3080_NETWORK");
    scan_results[0].rssi = -50;  /* Strong signal */
    scan_results[0].channel = 1;
    scan_results[0].security = WIFI_SECURITY_TYPE_PSK;
    memcpy(scan_results[0].mac, mock_mac, 6);
    
    /* Mock network 2 */
    strcpy(scan_results[1].ssid, "OpenWiFi");
    scan_results[1].ssid_length = strlen("OpenWiFi");
    scan_results[1].rssi = -70;  /* Medium signal */
    scan_results[1].channel = 6;
    scan_results[1].security = WIFI_SECURITY_TYPE_NONE;
    memcpy(scan_results[1].mac, mock_mac, 6);
    scan_results[1].mac[5] += 1;  /* Change last byte for uniqueness */
    
    /* Mock network 3 */
    strcpy(scan_results[2].ssid, "SecureNet");
    scan_results[2].ssid_length = strlen("SecureNet");
    scan_results[2].rssi = -85;  /* Weak signal */
    scan_results[2].channel = 11;
    scan_results[2].security = WIFI_SECURITY_TYPE_PSK;
    memcpy(scan_results[2].mac, mock_mac, 6);
    scan_results[2].mac[5] += 2;  /* Change last byte for uniqueness */

    /* Now deliver the "scan results" after a short delay */
    k_timeout_t delay = K_MSEC(100);
    k_work_schedule_for_queue(&k_sys_work_q, &deliver_scan_result_work, delay);
    
    return 0;
}

/* Function to connect to WiFi network */
int emw3080_mgmt_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
    LOG_INF("EMW3080 mock WiFi connect: SSID=%.*s", params->ssid_length, params->ssid);
    
    /* Update the current status to show as connected */
    memcpy(current_status.ssid, params->ssid, params->ssid_length);
    current_status.ssid[params->ssid_length] = '\0';
    current_status.ssid_len = params->ssid_length;
    current_status.state = WIFI_STATE_ASSOCIATED;
    current_status.security = params->security;
    
    /* Return success */
    return 0;
}

/* Function to disconnect from WiFi network */
int emw3080_mgmt_disconnect(const struct device *dev)
{
    LOG_INF("EMW3080 mock WiFi disconnect");
    
    /* Update status to show as disconnected */
    current_status.state = WIFI_STATE_DISCONNECTED;
    current_status.ssid_len = 0;
    current_status.ssid[0] = '\0';
    
    return 0;
}

/* Function to get WiFi status */
int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status)
{
    LOG_INF("EMW3080 mock WiFi get status");
    
    /* Copy the current status to the output parameter */
    memcpy(status, &current_status, sizeof(struct wifi_iface_status));
    
    return 0;
}

/* Initialize the management interface */
void emw3080_mgmt_init(void)
{
    LOG_INF("EMW3080 WiFi management interface initialized");
}

/* Function to set the network interface */
void emw3080_mgmt_set_iface(struct net_if *net_iface)
{
    scan_iface = net_iface;
    LOG_INF("WiFi interface set");
}
