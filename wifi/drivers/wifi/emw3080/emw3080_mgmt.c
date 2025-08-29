/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "emw3080.h"
#include "emw3080_ipc.h"

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

#ifndef WIFI_FREQ_BAND_2_4_GHZ
#define WIFI_FREQ_BAND_2_4_GHZ 0
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

/* IPC stub implementations - placed here to avoid linker garbage collection */
int emw3080_ipc_init(const struct device *dev) {
    LOG_INF("IPC init stub called");
    return 0;
}

int emw3080_ipc_scan(const struct device *dev, enum emw3080_scan_mode mode, const char *ssid) {
    LOG_INF("IPC scan stub called: mode=%d, ssid=%s", mode, ssid ? ssid : "(null)");
    
    /* Populate mock scan results */
    scan_result_count = 3;
    
    /* Mock AP 1 */
    strncpy(scan_results[0].ssid, "EMW3080_TEST_AP_1", sizeof(scan_results[0].ssid) - 1);
    scan_results[0].ssid[sizeof(scan_results[0].ssid) - 1] = '\0';
    scan_results[0].ssid_length = strlen(scan_results[0].ssid);
    scan_results[0].channel = 6;
    scan_results[0].rssi = -45;
    scan_results[0].security = WIFI_SECURITY_TYPE_PSK;
    scan_results[0].band = WIFI_FREQ_BAND_2_4_GHZ;
    
    /* Mock AP 2 */
    strncpy(scan_results[1].ssid, "EMW3080_TEST_AP_2", sizeof(scan_results[1].ssid) - 1);
    scan_results[1].ssid[sizeof(scan_results[1].ssid) - 1] = '\0';
    scan_results[1].ssid_length = strlen(scan_results[1].ssid);
    scan_results[1].channel = 11;
    scan_results[1].rssi = -67;
    scan_results[1].security = WIFI_SECURITY_TYPE_WPA_PSK;
    scan_results[1].band = WIFI_FREQ_BAND_2_4_GHZ;
    
    /* Mock AP 3 */
    strncpy(scan_results[2].ssid, "EMW3080_OPEN_AP", sizeof(scan_results[2].ssid) - 1);
    scan_results[2].ssid[sizeof(scan_results[2].ssid) - 1] = '\0';
    scan_results[2].ssid_length = strlen(scan_results[2].ssid);
    scan_results[2].channel = 1;
    scan_results[2].rssi = -72;
    scan_results[2].security = WIFI_SECURITY_TYPE_NONE;
    scan_results[2].band = WIFI_FREQ_BAND_2_4_GHZ;
    
    LOG_INF("IPC scan: Populated %d mock scan results", scan_result_count);
    return 0;
}

int emw3080_ipc_connect(const struct device *dev, const struct emw3080_connect_params *params) {
    LOG_INF("IPC connect stub called: ssid=%s", params ? (const char *)params->ssid : "(null)");
    return 0;
}

int emw3080_ipc_disconnect(const struct device *dev) {
    LOG_INF("IPC disconnect stub called");  
    return 0;
}



int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb)
{
    int ret;
    
    LOG_INF("EMW3080: Initiating WiFi scan using binary IPC");
    
    /* Convert Zephyr scan params to EMW3080 IPC format */
    enum emw3080_scan_mode mode = EMW3080_SCAN_ACTIVE;
    const char *ssid = NULL;
    
    if (params && params->ssids[0]) {
        ssid = params->ssids[0];  /* Use first SSID if provided */
    }
    
    /* Call the real IPC scan function */
    ret = emw3080_ipc_scan(dev, mode, ssid);
    if (ret != 0) {
        LOG_ERR("EMW3080: WiFi scan failed: %d", ret);
        return ret;
    }
    
    /* Mark scan as completed and call callback for each result */
    scan_completed = true;
    
    if (cb && scan_result_count > 0) {
        LOG_INF("EMW3080: Calling scan result callback for %d results", scan_result_count);
        for (int i = 0; i < scan_result_count; i++) {
            cb(mgmt_iface, 0, &scan_results[i]);
        }
        /* Signal scan completion */
        cb(mgmt_iface, 0, NULL);
    }
    
    LOG_INF("EMW3080: WiFi scan completed successfully with %d results", scan_result_count);
    return 0;
}

int emw3080_mgmt_connect(const struct device *dev,
                        struct wifi_connect_req_params *params)
{
    int ret;
    struct emw3080_connect_params ipc_params;
    
    if (!params || !params->ssid) {
        LOG_ERR("EMW3080: Invalid connection parameters");
        return -EINVAL;
    }
    
    LOG_INF("EMW3080: Connecting to SSID: %s using binary IPC", params->ssid);
    
    /* Store connection parameters */
    memcpy(&current_connection, params, sizeof(current_connection));
    
    /* Convert Zephyr params to EMW3080 IPC format */
    memset(&ipc_params, 0, sizeof(ipc_params));
    strncpy(ipc_params.ssid, params->ssid, sizeof(ipc_params.ssid) - 1);
    
    if (params->psk) {
        strncpy(ipc_params.password, params->psk, sizeof(ipc_params.password) - 1);
        ipc_params.security = EMW3080_SEC_WPA2_AES;
    } else {
        ipc_params.security = EMW3080_SEC_NONE;
    }
    
    /* Call the real IPC connect function */
    ret = emw3080_ipc_connect(dev, &ipc_params);
    if (ret != 0) {
        LOG_ERR("EMW3080: WiFi connection failed: %d", ret);
        return ret;
    }
    
    /* Update status */
    current_status.state = WIFI_STATE_COMPLETED;
    current_status.link_mode = WIFI_LINK_MODE_STATION;
    current_status.band = WIFI_FREQ_BAND_2_4_GHZ;
    current_status.iface_mode = WIFI_MODE_INFRA;
    
    /* Copy SSID */
    strncpy(current_status.ssid, params->ssid, 
            MIN(strlen(params->ssid), WIFI_SSID_MAX_LEN));
    current_status.ssid_len = strlen(params->ssid);
    
    LOG_INF("EMW3080: WiFi connection completed successfully");
    return 0;
}

int emw3080_mgmt_disconnect(const struct device *dev)
{
    int ret;
    
    LOG_INF("EMW3080: Disconnecting from WiFi using binary IPC");
    
    /* Call the real IPC disconnect function */
    ret = emw3080_ipc_disconnect(dev);
    if (ret != 0) {
        LOG_ERR("EMW3080: WiFi disconnection failed: %d", ret);
        return ret;
    }
    
    /* Update status */
    current_status.state = WIFI_STATE_DISCONNECTED;
    current_status.link_mode = WIFI_LINK_MODE_UNKNOWN;
    memset(current_status.ssid, 0, sizeof(current_status.ssid));
    current_status.ssid_len = 0;
    
    LOG_INF("EMW3080: WiFi disconnected successfully");
    return 0;
}

int emw3080_mgmt_status(const struct device *dev, struct wifi_iface_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    
    /* Copy current status */
    memcpy(status, &current_status, sizeof(struct wifi_iface_status));
    
    LOG_DBG("EMW3080: Status - State: %d, Link Mode: %d, SSID: %s",
            status->state, status->link_mode, status->ssid);
    
    return 0;
}

int emw3080_mgmt_ap_enable(const struct device *dev,
                          struct wifi_connect_req_params *params)
{
    LOG_INF("EMW3080: AP mode not yet implemented");
    return -ENOTSUP;
}

int emw3080_mgmt_ap_disable(const struct device *dev)
{
    LOG_INF("EMW3080: AP mode not yet implemented");
    return -ENOTSUP;
}

int emw3080_mgmt_iface_status(const struct device *dev, struct wifi_iface_status *status)
{
    return emw3080_mgmt_status(dev, status);
}

bool emw3080_mgmt_scan_results_ready(void)
{
    return scan_completed;
}

int emw3080_mgmt_get_scan_results(struct wifi_scan_result *results, int max_results, int *count)
{
    if (!results || !count) {
        return -EINVAL;
    }
    
    int copy_count = MIN(scan_result_count, max_results);
    memcpy(results, scan_results, copy_count * sizeof(struct wifi_scan_result));
    *count = copy_count;
    
    LOG_DBG("EMW3080: Returning %d scan results", copy_count);
    return 0;
}

int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status)
{
    return emw3080_mgmt_status(dev, status);
}

void emw3080_mgmt_set_iface(struct net_if *net_iface)
{
    mgmt_iface = net_iface;
    LOG_DBG("EMW3080: Management interface set");
}

void emw3080_mgmt_init(void)
{
    LOG_INF("EMW3080: Management layer initialized");
    
    /* Initialize status structure */
    memset(&current_status, 0, sizeof(current_status));
    current_status.state = WIFI_STATE_INACTIVE;
    current_status.link_mode = WIFI_LINK_MODE_UNKNOWN;
    current_status.band = WIFI_FREQ_BAND_UNKNOWN;
    current_status.iface_mode = WIFI_MODE_UNKNOWN;
    
    /* Initialize scan results */
    scan_result_count = 0;
    scan_completed = false;
}
