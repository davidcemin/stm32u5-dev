/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "emw3080.h"
#include "emw3080_ipc.h"
#include "emw3080_spi.h"

LOG_MODULE_REGISTER(emw3080_mgmt, CONFIG_WIFI_LOG_LEVEL);

/* Define missing WiFi constants */
#ifndef WIFI_LINK_MODE_STATION
#define WIFI_LINK_MODE_STATION 1
#endif

#ifndef WIFI_LINK_MODE_UNKNOWN
#define WIFI_LINK_MODE_UNKNOWN 0
#endif

#ifndef WIFI_MODE_INFRA
#define WIFI_MODE_INFRA 2
#endif

#ifndef WIFI_FREQ_BAND_2_4_GHZ
#define WIFI_FREQ_BAND_2_4_GHZ 0
#endif

#ifndef WIFI_MODE_UNKNOWN
#define WIFI_MODE_UNKNOWN 0
#endif

#ifndef WIFI_FREQ_BAND_5_GHZ
#define WIFI_FREQ_BAND_5_GHZ 1
#endif

/* Status and state tracking */
static struct wifi_iface_status current_status;
static struct wifi_connect_req_params current_connection;
static struct net_if *mgmt_iface = NULL;

/* Scan results storage */
static struct wifi_scan_result scan_results[10];
static int scan_result_count = 0;
static bool scan_completed = false;

/* EMW3080 WiFi Management Functions */

int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb)
{
    int ret;
    
    LOG_INF("EMW3080: Initiating WiFi scan using SLIP-enhanced IPC");
    
    /* Convert Zephyr scan params to EMW3080 IPC format */
    enum emw3080_scan_mode mode = EMW3080_SCAN_ACTIVE;
    const char *ssid = NULL;
    
    if (params && params->ssids[0]) {
        ssid = params->ssids[0];  /* Use first SSID if provided */
    }
    
    /* Call the SLIP-enhanced IPC scan function */
    ret = emw3080_ipc_scan(dev, mode, ssid);
    if (ret != 0) {
        LOG_ERR("EMW3080: WiFi scan failed: %d", ret);
        return ret;
    }
    
    /* Wait a moment for scan to complete */
    k_msleep(100);
    
    /* Get scan results from the device */
    struct emw3080_ap_info ap_results[10];
    int num_aps = emw3080_ipc_get_scan_results(dev, ap_results, 10);
    
    if (num_aps < 0) {
        LOG_ERR("EMW3080: Failed to get scan results: %d", num_aps);
        return num_aps;
    }
    
    /* Convert EMW3080 results to Zephyr format and store */
    scan_result_count = (num_aps > 10) ? 10 : num_aps;
    
    for (int i = 0; i < scan_result_count; i++) {
        /* Copy SSID */
        strncpy(scan_results[i].ssid, (char *)ap_results[i].ssid, 
                sizeof(scan_results[i].ssid) - 1);
        scan_results[i].ssid[sizeof(scan_results[i].ssid) - 1] = '\0';
        
        /* Copy BSSID */
        memcpy(scan_results[i].mac, ap_results[i].bssid, 6);
        scan_results[i].mac_length = 6;
        
        /* Set other fields */
        scan_results[i].channel = ap_results[i].channel;
        scan_results[i].rssi = ap_results[i].rssi;
        scan_results[i].security = ap_results[i].security;
        scan_results[i].band = WIFI_FREQ_BAND_2_4_GHZ;
        scan_results[i].mfp = WIFI_MFP_UNKNOWN;
    }
    
    scan_completed = true;
    
    /* Call the scan result callback for each AP found */
    if (cb && mgmt_iface) {
        for (int i = 0; i < scan_result_count; i++) {
            cb(mgmt_iface, 0, &scan_results[i]);
        }
        /* Signal scan completion */
        cb(mgmt_iface, 0, NULL);
    }
    
    LOG_INF("EMW3080: Scan completed - found %d access points", scan_result_count);
    return 0;
}

int emw3080_mgmt_connect(const struct device *dev,
                        struct wifi_connect_req_params *params)
{
    int ret;
    struct emw3080_connect_params ipc_params;
    
    LOG_INF("EMW3080: Connecting to WiFi network '%s' using SLIP-enhanced IPC", 
            params->ssid ? (char *)params->ssid : "(null)");
    
    if (!params || !params->ssid || params->ssid_length == 0) {
        LOG_ERR("Invalid connection parameters");
        return -EINVAL;
    }
    
    /* Convert Zephyr connect params to EMW3080 IPC format */
    memset(&ipc_params, 0, sizeof(ipc_params));
    
    /* Copy SSID */
    strncpy((char *)ipc_params.ssid, (char *)params->ssid, 32);
    ipc_params.ssid[32] = '\0';
    
    /* Copy password if provided */
    if (params->psk && params->psk_length > 0) {
        strncpy((char *)ipc_params.password, (char *)params->psk, 64);
        ipc_params.password[64] = '\0';
        ipc_params.security = EMW3080_SEC_WPA2_AES;  /* Default to WPA2 */
    } else {
        ipc_params.security = EMW3080_SEC_NONE;  /* Open network */
    }
    
    ipc_params.dhcp_enabled = 1;  /* Enable DHCP by default */
    
    /* Store connection parameters for status tracking */
    memcpy(&current_connection, params, sizeof(current_connection));
    
    /* Call the SLIP-enhanced IPC connect function */
    ret = emw3080_ipc_connect(dev, &ipc_params);
    if (ret != 0) {
        LOG_ERR("EMW3080: WiFi connection failed: %d", ret);
        return ret;
    }
    
    LOG_INF("EMW3080: WiFi connection initiated successfully");
    return 0;
}

int emw3080_mgmt_disconnect(const struct device *dev)
{
    LOG_INF("EMW3080: Disconnecting from WiFi network using SLIP-enhanced IPC");
    
    /* Call the SLIP-enhanced IPC disconnect function */
    int ret = emw3080_ipc_disconnect(dev);
    if (ret != 0) {
        LOG_ERR("EMW3080: WiFi disconnection failed: %d", ret);
        return ret;
    }
    
    /* Clear connection status */
    memset(&current_connection, 0, sizeof(current_connection));
    current_status.state = WIFI_STATE_DISCONNECTED;
    
    LOG_INF("EMW3080: WiFi disconnected successfully");
    return 0;
}

int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status)
{
    LOG_DBG("EMW3080: Getting WiFi status");
    
    if (!status) {
        return -EINVAL;
    }
    
    /* Copy current status */
    memcpy(status, &current_status, sizeof(*status));
    
    return 0;
}

bool emw3080_mgmt_scan_results_ready(void)
{
    return scan_completed;
}

int emw3080_mgmt_get_scan_results(struct wifi_scan_result *results, int max_results, int *count)
{
    LOG_DBG("EMW3080: Getting cached scan results");
    
    if (!results || !count || max_results <= 0) {
        return -EINVAL;
    }
    
    int num_to_copy = (scan_result_count < max_results) ? scan_result_count : max_results;
    
    for (int i = 0; i < num_to_copy; i++) {
        memcpy(&results[i], &scan_results[i], sizeof(struct wifi_scan_result));
    }
    
    *count = num_to_copy;
    
    LOG_INF("EMW3080: Returned %d cached scan results", num_to_copy);
    return 0;
}

int emw3080_mgmt_init(const struct device *dev)
{
    LOG_INF("EMW3080: Initializing management interface with SLIP protocol support");
    
    /* Initialize status */
    memset(&current_status, 0, sizeof(current_status));
    current_status.state = WIFI_STATE_DISCONNECTED;
    current_status.link_mode = WIFI_LINK_MODE_UNKNOWN;
    current_status.band = WIFI_FREQ_BAND_2_4_GHZ;
    current_status.iface_mode = WIFI_MODE_INFRA;
    
    /* Initialize connection params */
    memset(&current_connection, 0, sizeof(current_connection));
    
    /* Reset scan state */
    scan_result_count = 0;
    scan_completed = false;
    
    return 0;
}

int emw3080_mgmt_set_iface(struct net_if *iface)
{
    mgmt_iface = iface;
    LOG_INF("EMW3080: Management interface set to %p", iface);
    return 0;
}

/* Stub functions for compatibility */
int emw3080_mgmt_status(const struct device *dev, struct wifi_iface_status *status)
{
    return emw3080_mgmt_get_status(dev, status);
}

int emw3080_mgmt_ap_enable(const struct device *dev, struct wifi_connect_req_params *params)
{
    LOG_INF("EMW3080: AP mode not supported");
    return -ENOTSUP;
}

int emw3080_mgmt_ap_disable(const struct device *dev)
{
    LOG_INF("EMW3080: AP mode not supported");
    return -ENOTSUP;
}

int emw3080_mgmt_iface_status(const struct device *dev, struct wifi_iface_status *status)
{
    return emw3080_mgmt_get_status(dev, status);
}
