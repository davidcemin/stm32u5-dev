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
    LOG_INF("Delivering scan results...");
    
    if (!active_scan_cb) {
        LOG_ERR("No active scan callback");
        return;
    }
    
    if (!scan_iface) {
        LOG_ERR("No scan interface");
        
        /* Try to get the default interface as fallback */
        scan_iface = net_if_get_default();
        if (!scan_iface) {
            LOG_ERR("No fallback interface available");
            return;
        }
        LOG_WRN("Using default interface as fallback");
    }
    
    LOG_INF("Delivering %d scan results to callback at %p", scan_result_count, active_scan_cb);
    
    /* Deliver each scan result */
    for (int i = 0; i < scan_result_count && i < EMW3080_MAX_SCAN_RESULTS; i++) {
        LOG_INF("Delivering scan result %d: SSID=%s", i, scan_results[i].ssid);
        
        /* Validate the scan result before delivering */
        if (scan_results[i].ssid_length == 0 || scan_results[i].ssid_length > 32) {
            LOG_WRN("Invalid SSID length for result %d, fixing", i);
            scan_results[i].ssid_length = strlen(scan_results[i].ssid);
            if (scan_results[i].ssid_length > 32) {
                scan_results[i].ssid_length = 32;
                scan_results[i].ssid[32] = '\0';
            }
        }
        
        /* Make the call within a try/catch context if available */
        active_scan_cb(scan_iface, 0, &scan_results[i]);
        k_sleep(K_MSEC(10));  /* Small delay between results */
    }
    
    LOG_INF("Sending NULL result to signal end of scan");
    
    /* Signal the end of the scan */
    active_scan_cb(scan_iface, 0, NULL);
    
    LOG_INF("Scan results delivery complete");
    
    /* Clear the active callback */
    active_scan_cb = NULL;
}

/* Initialize the work item */
K_WORK_DELAYABLE_DEFINE(deliver_scan_result_work, deliver_scan_results_handler);

/* Function to perform a mock WiFi scan */
int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb)
{
    LOG_INF("EMW3080 mock WiFi scan initiated");
    
    /* Validate parameters */
    if (!dev) {
        LOG_ERR("Invalid device parameter");
        return -EINVAL;
    }
    
    if (!cb) {
        LOG_ERR("Invalid callback parameter");
        return -EINVAL;
    }
    
    /* Store callback for use when delivering results */
    active_scan_cb = cb;
    
    /* Use stored interface if available, otherwise look it up */
    if (!scan_iface) {
        LOG_INF("No stored interface, looking up interface for device");
        
        /* Find the network interface for this device */
        for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
            struct net_if *iface = net_if_get_by_index(i);
            if (!iface) {
                continue;
            }
            
            const struct device *iface_dev = net_if_get_device(iface);
            if (iface_dev == dev) {
                scan_iface = iface;
                LOG_INF("Found matching interface at index %d", i);
                break;
            }
        }
    }
    
    /* If we still don't have an interface, use the one from set_iface */
    if (!scan_iface) {
        LOG_WRN("Could not find interface for scan by device match");
        
        /* Try to get default interface */
        scan_iface = net_if_get_default();
        if (scan_iface) {
            LOG_INF("Using default interface for scan");
        } else {
            LOG_ERR("No interface available for scan");
            return -ENODEV;
        }
    }
    
    /* For testing, generate some mock scan results */
    scan_result_count = 3;  /* Return 3 mock networks */
    
    /* Mock network 1 */
    strncpy(scan_results[0].ssid, "EMW3080_NETWORK", sizeof(scan_results[0].ssid) - 1);
    scan_results[0].ssid[sizeof(scan_results[0].ssid) - 1] = '\0';
    scan_results[0].ssid_length = strlen("EMW3080_NETWORK");
    scan_results[0].rssi = -50;  /* Strong signal */
    scan_results[0].channel = 1;
    scan_results[0].security = WIFI_SECURITY_TYPE_PSK;
    memcpy(scan_results[0].mac, mock_mac, 6);
    
    /* Mock network 2 */
    strncpy(scan_results[1].ssid, "OpenWiFi", sizeof(scan_results[1].ssid) - 1);
    scan_results[1].ssid[sizeof(scan_results[1].ssid) - 1] = '\0';
    scan_results[1].ssid_length = strlen("OpenWiFi");
    scan_results[1].rssi = -70;  /* Medium signal */
    scan_results[1].channel = 6;
    scan_results[1].security = WIFI_SECURITY_TYPE_NONE;
    memcpy(scan_results[1].mac, mock_mac, 6);
    scan_results[1].mac[5] += 1;  /* Change last byte for uniqueness */
    
    /* Mock network 3 */
    strncpy(scan_results[2].ssid, "SecureNet", sizeof(scan_results[2].ssid) - 1);
    scan_results[2].ssid[sizeof(scan_results[2].ssid) - 1] = '\0';
    scan_results[2].ssid_length = strlen("SecureNet");
    scan_results[2].rssi = -85;  /* Weak signal */
    scan_results[2].channel = 11;
    scan_results[2].security = WIFI_SECURITY_TYPE_PSK;
    memcpy(scan_results[2].mac, mock_mac, 6);
    scan_results[2].mac[5] += 2;  /* Change last byte for uniqueness */

    /* Now deliver the "scan results" after a short delay */
    LOG_INF("Scheduling scan results delivery in 100ms");
    k_timeout_t delay = K_MSEC(100);
    k_work_schedule_for_queue(&k_sys_work_q, &deliver_scan_result_work, delay);
    
    return 0;
}

/* Function to connect to WiFi network */
int emw3080_mgmt_connect(const struct device *dev, struct wifi_connect_req_params *params)
{
    /* Validate parameters */
    if (!dev) {
        LOG_ERR("Invalid device parameter in connect");
        return -EINVAL;
    }
    
    if (!params) {
        LOG_ERR("Invalid connection parameters");
        return -EINVAL;
    }
    
    /* Validate SSID length */
    if (params->ssid_length == 0 || params->ssid_length > 32) {
        LOG_ERR("Invalid SSID length: %d", params->ssid_length);
        return -EINVAL;
    }
    
    /* Validate PSK if security requires it */
    if (params->security == WIFI_SECURITY_TYPE_PSK && 
        (params->psk_length == 0 || params->psk == NULL)) {
        LOG_ERR("Missing PSK for secured network");
        return -EINVAL;
    }
    
    LOG_INF("EMW3080 mock WiFi connect: SSID=%.*s", params->ssid_length, params->ssid);
    
    /* Update the current status to show as connected */
    memset(current_status.ssid, 0, sizeof(current_status.ssid));
    memcpy(current_status.ssid, params->ssid, 
          params->ssid_length > sizeof(current_status.ssid) - 1 ? 
          sizeof(current_status.ssid) - 1 : params->ssid_length);
    
    current_status.ssid_len = params->ssid_length < sizeof(current_status.ssid) ?
                             params->ssid_length : sizeof(current_status.ssid) - 1;
    current_status.state = WIFI_STATE_ASSOCIATED;
    current_status.security = params->security;
    
    /* Update more status fields */
    current_status.channel = params->channel == WIFI_CHANNEL_ANY ? 1 : params->channel;
    current_status.band = WIFI_FREQ_BAND_2_4_GHZ;  /* Fixed for now */
    current_status.rssi = -50;  /* Mock strong signal */
    
    /* Return success */
    return 0;
}

/* Function to disconnect from WiFi network */
int emw3080_mgmt_disconnect(const struct device *dev)
{
    /* Validate parameters */
    if (!dev) {
        LOG_ERR("Invalid device parameter in disconnect");
        return -EINVAL;
    }
    
    LOG_INF("EMW3080 mock WiFi disconnect");
    
    /* Update status to show as disconnected */
    current_status.state = WIFI_STATE_DISCONNECTED;
    current_status.ssid_len = 0;
    memset(current_status.ssid, 0, sizeof(current_status.ssid));
    current_status.rssi = -90;  /* No signal */
    
    return 0;
}

/* Function to get WiFi status */
int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status)
{
    LOG_INF("EMW3080 mock WiFi get status");
    
    /* Safety check to prevent null pointer dereference */
    if (!dev) {
        LOG_ERR("Invalid device parameter to emw3080_mgmt_get_status: dev=%p", dev);
        return -EINVAL;
    }
    
    if (!status) {
        LOG_ERR("Invalid status parameter to emw3080_mgmt_get_status: status=%p", status);
        return -EINVAL;
    }
    
    /* Start with a completely zeroed structure to be safe */
    memset(status, 0, sizeof(struct wifi_iface_status));
    
    /* Set all fields individually rather than copying a structure */
    status->state = WIFI_STATE_ASSOCIATED; /* Pretend we're connected */
    
    /* Set a safe SSID */
    const char *test_ssid = "EMW3080-TEST";
    size_t len = strlen(test_ssid);
    if (len >= sizeof(status->ssid)) {
        len = sizeof(status->ssid) - 1; /* Ensure space for null terminator */
    }
    
    memcpy(status->ssid, test_ssid, len);
    status->ssid[len] = '\0';
    status->ssid_len = len;
    
    /* Set other fields to safe default values */
    status->band = WIFI_FREQ_BAND_2_4_GHZ;
    status->channel = 1;
    status->security = WIFI_SECURITY_TYPE_PSK;
    status->rssi = -65;
    status->iface_mode = WIFI_STA_MODE;
    status->mfp = WIFI_MFP_DISABLE;
    
    /* Log what we're returning for debugging */
    LOG_INF("WiFi status returning: SSID=%s (%d), State=%d, RSSI=%d",
            status->ssid, status->ssid_len, status->state, status->rssi);
            
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
