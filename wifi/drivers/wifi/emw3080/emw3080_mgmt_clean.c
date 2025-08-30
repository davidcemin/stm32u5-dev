/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "emw3080.h"
#include "emw3080_ipc.h"

LOG_MODULE_REGISTER(emw3080_mgmt, CONFIG_WIFI_LOG_LEVEL);

/* Current WiFi status */
static struct wifi_status current_status;
static struct wifi_connect_req_params current_connection;

int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb)
{
    int ret;
    
    LOG_INF("EMW3080: Initiating WiFi scan using binary IPC");
    
    /* Call the real IPC scan function */
    ret = emw3080_ipc_scan(dev, params, cb);
    if (ret != 0) {
        LOG_ERR("EMW3080: WiFi scan failed: %d", ret);
        return ret;
    }
    
    LOG_INF("EMW3080: WiFi scan completed successfully");
    return 0;
}

int emw3080_mgmt_connect(const struct device *dev,
                        struct wifi_connect_req_params *params)
{
    int ret;
    
    if (!params || !params->ssid) {
        LOG_ERR("EMW3080: Invalid connection parameters");
        return -EINVAL;
    }
    
    LOG_INF("EMW3080: Connecting to SSID: %s using binary IPC", params->ssid);
    
    /* Store connection parameters */
    memcpy(&current_connection, params, sizeof(current_connection));
    
    /* Call the real IPC connect function */
    ret = emw3080_ipc_connect(dev, params);
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

int emw3080_mgmt_init(const struct device *dev)
{
    LOG_INF("EMW3080: Management layer initialized");
    
    /* Initialize status structure */
    memset(&current_status, 0, sizeof(current_status));
    current_status.state = WIFI_STATE_INACTIVE;
    current_status.link_mode = WIFI_LINK_MODE_UNKNOWN;
    current_status.band = WIFI_FREQ_BAND_UNKNOWN;
    current_status.iface_mode = WIFI_MODE_UNKNOWN;
    
    return 0;
}
