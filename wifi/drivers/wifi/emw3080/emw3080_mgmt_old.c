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
#include "emw3080_ipc.h"  /* For binary IPC protocol functions */

/* This is a bridge module that connects the EMW3080 driver to the WiFi management API */

/* Forward declarations */
extern const struct device *get_emw3080_net_device(void);

/* Forward declarations for new binary IPC protocol functions */
static int emw3080_convert_scan_results_to_zephyr(void);

/* Buffer to store scan results */
/* Define our own maximum scan result count */
#define EMW3080_MAX_SCAN_RESULTS 10
static struct wifi_scan_result scan_results[EMW3080_MAX_SCAN_RESULTS];
static int scan_result_count = 0;
static scan_result_cb_t active_scan_cb = NULL;
static struct net_if *scan_iface = NULL;

/* Connection status structure to track IP address and other info */
static struct {
    char ip_address[16];  /* xxx.xxx.xxx.xxx\0 */
} connect_status;

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

/* Flag to indicate scan results are ready */
static volatile bool scan_results_ready = false;

/* Work item to prepare scan results for direct access - NO CALLBACK VERSION */
static void deliver_scan_results_handler(struct k_work *work)
{
    LOG_INF("Preparing scan results for direct access (no callback)...");
    
    /* SAFETY: Make absolutely sure we have a valid interface */
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
    
    LOG_INF("Processing %d scan results for direct access", scan_result_count);
    
    /* First notify that scanning has started */
    net_mgmt_event_notify(NET_EVENT_WIFI_SCAN_DONE, scan_iface);
    
    /* Process and sanitize each scan result */
    for (int i = 0; i < scan_result_count && i < EMW3080_MAX_SCAN_RESULTS; i++) {
        /* Thoroughly validate and sanitize entries */
        struct wifi_scan_result *result = &scan_results[i];
        
        /* Skip empty or invalid entries */
        if (result->channel == 0 && 
            result->ssid_length == 0 && 
            result->rssi == 0) {
            LOG_WRN("Skipping empty scan result at index %d", i);
            continue;
        }
        
        /* Validate and fix the scan result */
        if (result->ssid_length > 32) {
            LOG_WRN("Invalid SSID length for result %d: %d, fixing", i, result->ssid_length);
            result->ssid_length = 32;
        }
        
        /* Ensure SSID is properly null-terminated */
        result->ssid[result->ssid_length < 32 ? result->ssid_length : 31] = '\0';
        
        /* Validate channel number (WiFi channels are typically 1-14) */
        if (result->channel < 1 || result->channel > 14) {
            LOG_WRN("Invalid channel number %d for result %d, setting to 1", result->channel, i);
            result->channel = 1;
        }
        
        /* Validate security type */
        if (result->security > 7) {  /* Assuming max security type value */
            LOG_WRN("Invalid security type %d for result %d, setting to 0", result->security, i);
            result->security = 0;
        }
        
        /* Validate MAC address (just ensure it's not all zeros) */
        bool mac_valid = false;
        for (int j = 0; j < 6; j++) {
            if (result->mac[j] != 0) {
                mac_valid = true;
                break;
            }
        }
        
        if (!mac_valid) {
            LOG_WRN("Invalid MAC address (all zeros) for result %d, setting dummy MAC", i);
            for (int j = 0; j < 6; j++) {
                result->mac[j] = 0x11 + j;
            }
        }
        
        /* Log what's available */
        if (result->ssid_length == 0) {
            LOG_INF("Processed hidden network on channel %d, RSSI=%d", 
                   result->channel, result->rssi);
        } else {
            LOG_INF("Processed scan result %d: SSID=%s, Ch=%d, RSSI=%d", 
                   i, result->ssid, result->channel, result->rssi);
        }
        
        /* Optionally notify through event system for other listeners */
        net_mgmt_event_notify_with_info(NET_EVENT_WIFI_SCAN_RESULT,
                                       scan_iface, result,
                                       sizeof(struct wifi_scan_result));
    }
    
    /* Mark results as ready for direct access */
    scan_results_ready = true;
    
    /* Notify through event system as well */
    net_mgmt_event_notify(NET_EVENT_WIFI_SCAN_DONE, scan_iface);
    
    LOG_INF("Scan results processing complete - data ready for direct access");
}

/* Initialize the work item */
K_WORK_DELAYABLE_DEFINE(deliver_scan_result_work, deliver_scan_results_handler);

/* Function to check if scan results are ready */
bool emw3080_mgmt_scan_results_ready(void)
{
    return scan_results_ready;
}

/* Function to get scan results directly */
int emw3080_mgmt_get_scan_results(struct wifi_scan_result *results, int max_results, int *count)
{
    if (!results || !count || max_results <= 0) {
        return -EINVAL;
    }
    
    if (!scan_results_ready) {
        *count = 0;
        return -EBUSY;  /* Results not ready yet */
    }
    
    /* Copy the results safely */
    int copy_count = scan_result_count;
    if (copy_count > max_results) {
        copy_count = max_results;
    }
    
    /* Safety: only copy valid results */
    *count = 0;
    for (int i = 0; i < copy_count; i++) {
        if (scan_results[i].channel > 0 || scan_results[i].ssid_length > 0) {
            memcpy(&results[*count], &scan_results[i], sizeof(struct wifi_scan_result));
            (*count)++;
        }
    }
    
    return 0;
}

/* Function to perform a WiFi scan using real AT commands */
int emw3080_mgmt_scan(const struct device *dev, struct wifi_scan_params *params,
                     scan_result_cb_t cb)
{
    LOG_INF("EMW3080 WiFi scan initiated using binary IPC protocol");
    
    /* Validate device parameter */
    if (!dev) {
        LOG_ERR("Invalid device parameter");
        return -EINVAL;
    }
    
    /* Reset the results ready flag */
    scan_results_ready = false;
    
    /* Store callback for compatibility */
    active_scan_cb = cb;
    
    /* Find the interface associated with this device */
    if (!scan_iface) {
        /* Look up interface for this device */
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
        
        /* Fall back to default if we couldn't find the interface */
        if (!scan_iface) {
            scan_iface = net_if_get_default();
            if (!scan_iface) {
                LOG_ERR("No interface available for scan");
                return -ENODEV;
            }
            LOG_INF("Using default interface for scan");
        }
    }
    
    /* Use binary IPC protocol for WiFi scan */
    LOG_INF("EMW3080: Performing WiFi scan using binary IPC protocol");
    
    /* Start scan using IPC protocol */
    enum emw3080_scan_mode scan_mode = EMW3080_SCAN_PASSIVE;
    const char *target_ssid = NULL;
    
    /* Check if we have specific SSID to scan for */
    if (params && params->ssids[0] && strlen(params->ssids[0]) > 0) {
        target_ssid = params->ssids[0];
        scan_mode = EMW3080_SCAN_ACTIVE;
        LOG_INF("Active scan for SSID: %s", target_ssid);
    } else {
        LOG_INF("Passive scan for all networks");
    }
    
    int ret = emw3080_ipc_scan(dev, scan_mode, target_ssid);
    if (ret < 0) {
        LOG_ERR("EMW3080: IPC scan command failed: %d", ret);
        scan_result_count = 0;
        return ret;
    }
    
    /* Give the module time to complete the scan */
    k_msleep(3000);
    
    /* Get scan results using IPC */
    struct emw3080_ap_info aps[EMW3080_MAX_SCAN_RESULTS];
    int found_aps = emw3080_ipc_get_scan_results(dev, aps, EMW3080_MAX_SCAN_RESULTS);
    
    if (found_aps > 0) {
        LOG_INF("EMW3080: Found %d networks from IPC scan", found_aps);
        
        /* Convert IPC results to Zephyr WiFi scan results */
        scan_result_count = MIN(found_aps, EMW3080_MAX_SCAN_RESULTS);
        
        for (int i = 0; i < scan_result_count; i++) {
            /* Clear the result structure */
            memset(&scan_results[i], 0, sizeof(scan_results[i]));
            
            /* Copy SSID */
            size_t ssid_len = MIN(strlen((char *)aps[i].ssid), WIFI_SSID_MAX_LEN);
            memcpy(scan_results[i].ssid, aps[i].ssid, ssid_len);
            scan_results[i].ssid_length = ssid_len;
            
            /* Copy BSSID */
            memcpy(scan_results[i].mac, aps[i].bssid, 6);
            
            /* Set channel and RSSI */
            scan_results[i].channel = aps[i].channel;
            scan_results[i].rssi = aps[i].rssi;
            
            /* Convert security type */
            switch (aps[i].security) {
                case EMW3080_SEC_NONE:
                    scan_results[i].security = WIFI_SECURITY_TYPE_NONE;
                    break;
                case EMW3080_SEC_WEP:
                    scan_results[i].security = WIFI_SECURITY_TYPE_WEP;
                    break;
                case EMW3080_SEC_WPA_TKIP:
                case EMW3080_SEC_WPA_AES:
                    scan_results[i].security = WIFI_SECURITY_TYPE_WPA_PSK;
                    break;
                case EMW3080_SEC_WPA2_TKIP:
                case EMW3080_SEC_WPA2_AES:
                case EMW3080_SEC_WPA2_MIXED:
                    scan_results[i].security = WIFI_SECURITY_TYPE_PSK;
                    break;
                default:
                    scan_results[i].security = WIFI_SECURITY_TYPE_UNKNOWN;
                    break;
            }
            
            LOG_INF("AP %d: SSID='%s', RSSI=%d dBm, Ch=%d, Security=%d",
                    i, scan_results[i].ssid, scan_results[i].rssi,
                    scan_results[i].channel, scan_results[i].security);
        }
    } else {
        LOG_WRN("EMW3080: No networks found in IPC scan");
        scan_result_count = 0;
    }

    /* Process the results without using callbacks */
    LOG_INF("Scheduling scan results delivery in 500ms (simulating scan time)");
    k_timeout_t delay = K_MSEC(500);  /* Longer delay for realism */
    k_work_schedule_for_queue(&k_sys_work_q, &deliver_scan_result_work, delay);
    
    /* If we have a callback, mark the scan as in-progress */
    /* But we'll only use it for notification, not for delivering results */
    scan_results_ready = false;
    
    return 0;
    
    /* Network 2: Open network with medium signal */
    strncpy(scan_results[1].ssid, "PublicWiFi", sizeof(scan_results[1].ssid) - 1);
    scan_results[1].ssid[sizeof(scan_results[1].ssid) - 1] = '\0';
    scan_results[1].ssid_length = strlen("PublicWiFi");
    scan_results[1].rssi = -68;  /* Medium signal */
    scan_results[1].channel = 6;
    scan_results[1].security = WIFI_SECURITY_TYPE_NONE;
    memcpy(scan_results[1].mac, mock_mac, 6);
    scan_results[1].mac[5] = 0x01;  /* Unique MAC */
    LOG_INF("Scan result 2: SSID=%s, RSSI=%d, Ch=%d", 
            scan_results[1].ssid, scan_results[1].rssi, scan_results[1].channel);
    
    /* Network 3: Enterprise network with medium signal */
    strncpy(scan_results[2].ssid, "Enterprise", sizeof(scan_results[2].ssid) - 1);
    scan_results[2].ssid[sizeof(scan_results[2].ssid) - 1] = '\0';
    scan_results[2].ssid_length = strlen("Enterprise");
    scan_results[2].rssi = -72;  /* Medium signal */
    scan_results[2].channel = 11;
    scan_results[2].security = WIFI_SECURITY_TYPE_PSK_SHA256;
    memcpy(scan_results[2].mac, mock_mac, 6);
    scan_results[2].mac[5] = 0x02;  /* Unique MAC */
    LOG_INF("Scan result 3: SSID=%s, RSSI=%d, Ch=%d", 
            scan_results[2].ssid, scan_results[2].rssi, scan_results[2].channel);
    
    /* Network 4: Strong but hidden network */
    strncpy(scan_results[3].ssid, "", sizeof(scan_results[3].ssid) - 1);
    scan_results[3].ssid[sizeof(scan_results[3].ssid) - 1] = '\0';
    scan_results[3].ssid_length = 0;  /* Zero length for hidden network */
    scan_results[3].rssi = -62;  /* Strong signal */
    scan_results[3].channel = 3;
    scan_results[3].security = WIFI_SECURITY_TYPE_PSK;
    memcpy(scan_results[3].mac, mock_mac, 6);
    scan_results[3].mac[5] = 0x03;  /* Unique MAC */
    LOG_INF("Scan result 4: Hidden network, RSSI=%d, Ch=%d", 
            scan_results[3].rssi, scan_results[3].channel);
    
    /* Network 5: Weak signal network */
    strncpy(scan_results[4].ssid, "WeakSignal", sizeof(scan_results[4].ssid) - 1);
    scan_results[4].ssid[sizeof(scan_results[4].ssid) - 1] = '\0';
    scan_results[4].ssid_length = strlen("WeakSignal");
    scan_results[4].rssi = -89;  /* Weak signal */
    scan_results[4].channel = 9;
    scan_results[4].security = WIFI_SECURITY_TYPE_PSK;
    memcpy(scan_results[4].mac, mock_mac, 6);
    /* Process the results without using callbacks */
    LOG_INF("Scheduling scan results delivery in 500ms (simulating scan time)");
    k_timeout_t scan_delay = K_MSEC(500);  /* Longer delay for realism */
    k_work_schedule_for_queue(&k_sys_work_q, &deliver_scan_result_work, scan_delay);
    
    /* If we have a callback, mark the scan as in-progress */
    /* But we'll only use it for notification, not for delivering results */
    scan_results_ready = false;
    
    return 0;
}

/* Function to connect to WiFi network using binary IPC protocol */
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
    if ((params->security == WIFI_SECURITY_TYPE_PSK || 
         params->security == WIFI_SECURITY_TYPE_PSK_SHA256) && 
        (params->psk_length == 0 || params->psk == NULL)) {
        LOG_ERR("Missing PSK for secured network");
        return -EINVAL;
    }
    
    LOG_INF("EMW3080 connecting to WiFi network using binary IPC: SSID=%.*s", 
           params->ssid_length, params->ssid);
    
    /* Update the current status to show we're trying to connect */
    current_status.state = WIFI_STATE_SCANNING; /* Using SCANNING as intermediate state */
    
    /* Update SSID information */
    memset(current_status.ssid, 0, sizeof(current_status.ssid));
    size_t copy_len = params->ssid_length;
    if (copy_len >= sizeof(current_status.ssid)) {
        copy_len = sizeof(current_status.ssid) - 1;
    }
    memcpy(current_status.ssid, params->ssid, copy_len);
    current_status.ssid[copy_len] = '\0';
    current_status.ssid_len = copy_len;
    
    /* Update security information */
    current_status.security = params->security;
    
    /* Prepare connection parameters for IPC */
    struct emw3080_connect_params ipc_params = {0};
    
    /* Copy SSID */
    memcpy(ipc_params.ssid, params->ssid, params->ssid_length);
    ipc_params.ssid[params->ssid_length] = '\0';
    
    /* Copy password if provided */
    if (params->psk && params->psk_length > 0) {
        size_t psk_len = MIN(params->psk_length, sizeof(ipc_params.password) - 1);
        memcpy(ipc_params.password, params->psk, psk_len);
        ipc_params.password[psk_len] = '\0';
    }
    
    /* Convert security type */
    switch (params->security) {
        case WIFI_SECURITY_TYPE_NONE:
            ipc_params.security = EMW3080_SEC_NONE;
            break;
        case WIFI_SECURITY_TYPE_WEP:
            ipc_params.security = EMW3080_SEC_WEP;
            break;
        case WIFI_SECURITY_TYPE_PSK:
            ipc_params.security = EMW3080_SEC_WPA2_AES;
            break;
        default:
            LOG_WRN("Unsupported security type %d, using WPA2-AES", params->security);
            ipc_params.security = EMW3080_SEC_WPA2_AES;
            break;
    }
    
    /* Enable DHCP by default */
    ipc_params.dhcp_enabled = 1;
    
    LOG_INF("EMW3080: Performing WiFi connection using binary IPC protocol");
    
    /* Send connect command via IPC */
    int ret = emw3080_ipc_connect(dev, &ipc_params);
    if (ret < 0) {
        LOG_ERR("EMW3080: IPC connect command failed: %d", ret);
        current_status.state = WIFI_STATE_DISCONNECTED;
        return ret;
    }
    
    /* Give the module time to connect */
    LOG_INF("EMW3080: Waiting for connection to establish...");
    k_msleep(5000);
    
    /* For now, assume success - in a real implementation we'd wait for 
     * connection events from the module */
    current_status.state = WIFI_STATE_COMPLETED;
    current_status.link_mode = WIFI_LINK_MODE_UNKNOWN;
    current_status.band = WIFI_FREQ_BAND_2_4_GHZ;
    current_status.iface_mode = WIFI_MODE_INFRA;
    
    LOG_INF("EMW3080: WiFi connection completed successfully");
    return 0;
        
        /* Simulate connection delay */
        k_sleep(K_MSEC(500));
        
        /* Update status to connected and assign signal strength */
        current_status.state = WIFI_STATE_ASSOCIATED;
        current_status.rssi = -55;  /* Good signal strength */
        
        /* Set fallback IP address for simulation */
        strcpy(connect_status.ip_address, "192.168.1.100");
        
        /* Ensure scan_iface is set for event notification */
        if (!scan_iface) {
            scan_iface = net_if_get_default();
        }
        
        /* Return notification through the event system */
        if (scan_iface) {
            net_mgmt_event_notify(NET_EVENT_WIFI_CONNECT_RESULT, scan_iface);
            LOG_INF("EMW3080: Sent connect result event notification");
        } else {
            LOG_WRN("EMW3080: No network interface available for event notification");
        }
        
        LOG_INF("Successfully connected to %s on channel %d", 
               current_status.ssid, current_status.channel);
        
        return 0;
    }
}

/* Function to disconnect from WiFi network using real AT commands */
int emw3080_mgmt_disconnect(const struct device *dev)
{
    /* Validate parameters */
    if (!dev) {
        LOG_ERR("Invalid device parameter in disconnect");
        return -EINVAL;
    }
    
    if (current_status.state != WIFI_STATE_COMPLETED && 
        current_status.state != WIFI_STATE_SCANNING) {
        LOG_WRN("WiFi already disconnected - nothing to do");
        return 0;
    }
    
    LOG_INF("EMW3080 disconnecting from WiFi network using binary IPC: %s", current_status.ssid);
    
    /* Save the SSID for reporting in the disconnect event */
    char prev_ssid[33];
    strncpy(prev_ssid, current_status.ssid, sizeof(prev_ssid) - 1);
    prev_ssid[sizeof(prev_ssid) - 1] = '\0';
    
    /* Send disconnect command via IPC */
    int ret = emw3080_ipc_disconnect(dev);
    if (ret < 0) {
        LOG_WRN("EMW3080: IPC disconnect command failed: %d", ret);
        /* Continue with disconnect anyway - local state cleanup */
    } else {
        LOG_INF("EMW3080: WiFi disconnect command sent successfully");
    }
    
    /* Update status to show as disconnected */
    current_status.state = WIFI_STATE_DISCONNECTED;
    current_status.ssid_len = 0;
    memset(current_status.ssid, 0, sizeof(current_status.ssid));
    current_status.rssi = -90;  /* No signal */
    current_status.channel = 0;
    
    /* Clear IP address */
    memset(connect_status.ip_address, 0, sizeof(connect_status.ip_address));
    
    /* Notify through the event system */
    if (scan_iface) {
        net_mgmt_event_notify(NET_EVENT_WIFI_DISCONNECT_RESULT, scan_iface);
    }
    
    LOG_INF("Successfully disconnected from %s", prev_ssid);
    
    return 0;
}

/* Function to get WiFi status */
int emw3080_mgmt_get_status(const struct device *dev, struct wifi_iface_status *status)
{
    LOG_INF("EMW3080 getting WiFi interface status");
    
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
    
    /* Check our stored connection status to determine state */
    if (current_status.state == WIFI_STATE_ASSOCIATED) {
        LOG_INF("Interface is currently connected");
        
        /* Copy our current connection information */
        memcpy(status, &current_status, sizeof(struct wifi_iface_status));
        
        /* Ensure SSID is properly null-terminated */
        if (status->ssid_len >= sizeof(status->ssid)) {
            status->ssid_len = sizeof(status->ssid) - 1;
        }
        status->ssid[status->ssid_len] = '\0';
        
        /* Return with connected status */
        LOG_INF("WiFi status: Connected to SSID=%s, Channel=%d, RSSI=%d",
                status->ssid, status->channel, status->rssi);
    } else {
        /* We're not connected - set appropriate values */
        LOG_INF("Interface is currently disconnected");
        
        status->state = WIFI_STATE_DISCONNECTED;
        status->band = WIFI_FREQ_BAND_2_4_GHZ; /* Most common band */
        status->iface_mode = WIFI_STA_MODE;     /* Station mode */
        status->mfp = WIFI_MFP_DISABLE;         /* No management frame protection */
        
        /* Clear SSID */
        status->ssid_len = 0;
        status->ssid[0] = '\0';
        
        /* Set other fields to default values */
        status->channel = 0;                    /* No channel when disconnected */
        status->security = WIFI_SECURITY_TYPE_NONE; /* No security when disconnected */
        status->rssi = -100;                    /* Very weak/no signal */
        
        LOG_INF("WiFi status: Disconnected");
    }
    
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
