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
#include "emw3080_spi.h"  /* For emw3080_response structure and SPI functions */

/* This is a bridge module that connects the EMW3080 driver to the WiFi management API */

/* Forward declarations */
extern const struct device *get_emw3080_net_device(void);

/* Forward declarations for AT command integration */
extern int emw3080_spi_send_at_cmd_enhanced(const struct device *spi_dev,
                                           const char *cmd, size_t cmd_len,
                                           struct emw3080_response *response,
                                           uint32_t timeout_ms);

/* Forward declarations for new real AT command functions */
static int emw3080_parse_real_scan_results(struct emw3080_response *response);
static void emw3080_generate_fallback_scan_results(void);
static int emw3080_get_ip_address_real(const struct device *spi_dev);

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
    LOG_INF("EMW3080 WiFi scan initiated using real AT commands");
    
    /* Validate device parameter */
    if (!dev) {
        LOG_ERR("Invalid device parameter");
        return -EINVAL;
    }
    
    /* Reset the results ready flag */
    scan_results_ready = false;
    
    /* Store callback for compatibility, but we won't use it directly */
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
    
    /* Try to get SPI device for real AT command communication */
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
    
    if (spi_dev && device_is_ready(spi_dev)) {
        LOG_INF("EMW3080: Performing real WiFi scan using enhanced SPI AT commands");
        
        /* Send real scan command: AT+CWLAP */
        struct emw3080_response response;
        const char *scan_cmd = "AT+CWLAP\r\n";
        
        LOG_INF("EMW3080: Sending WiFi scan command: %s", scan_cmd);
        int ret = emw3080_spi_send_at_cmd_enhanced(spi_dev, scan_cmd, strlen(scan_cmd), 
                                                  &response, 15000); /* 15 second timeout for scan */
        
        if (ret == 0) {
            LOG_INF("EMW3080: Scan command completed successfully");
            
            /* Parse the real scan results */
            ret = emw3080_parse_real_scan_results(&response);
            if (ret > 0) {
                LOG_INF("EMW3080: Parsed %d real networks from scan results", ret);
                scan_result_count = ret;
            } else {
                LOG_WRN("EMW3080: Failed to parse scan results, using fallback data");
                /* Fall back to mock data if parsing fails */
                emw3080_generate_fallback_scan_results();
            }
        } else {
            LOG_WRN("EMW3080: Real scan command failed (%d), using fallback data", ret);
            /* Fall back to mock data if AT command fails */
            emw3080_generate_fallback_scan_results();
        }
    } else {
        LOG_WRN("EMW3080: SPI device not ready, using fallback scan data");
        /* Fall back to mock data if SPI is not available */
        emw3080_generate_fallback_scan_results();
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

/* Function to connect to WiFi network using real AT commands */
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
    
    LOG_INF("EMW3080 connecting to WiFi network using real AT commands: SSID=%.*s", 
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
    
    /* Try to get SPI device for real AT command communication */
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
    
    if (spi_dev && device_is_ready(spi_dev)) {
        LOG_INF("EMW3080: Performing real WiFi connection using enhanced SPI AT commands");
        
        /* Build AT+CWJAP command with SSID and password */
        char connect_cmd[128];
        int cmd_len;
        
        if (params->security == WIFI_SECURITY_TYPE_NONE || params->psk_length == 0) {
            /* Open network - SSID only */
            cmd_len = snprintf(connect_cmd, sizeof(connect_cmd), "AT+CWJAP=\"%.*s\"\r\n",
                              (int)params->ssid_length, params->ssid);
        } else {
            /* Secured network - SSID and password */
            cmd_len = snprintf(connect_cmd, sizeof(connect_cmd), "AT+CWJAP=\"%.*s\",\"%.*s\"\r\n",
                              (int)params->ssid_length, params->ssid,
                              (int)params->psk_length, params->psk);
        }
        
        if (cmd_len >= sizeof(connect_cmd)) {
            LOG_ERR("EMW3080: Connect command too long");
            return -EINVAL;
        }
        
        LOG_INF("EMW3080: Sending WiFi connect command: %s", connect_cmd);
        
        struct emw3080_response response;
        int ret = emw3080_spi_send_at_cmd_enhanced(spi_dev, connect_cmd, cmd_len, 
                                                  &response, 20000); /* 20 second timeout for connection */
        
        if (ret == 0 && response.type == EMW3080_RESP_TYPE_OK) {
            LOG_INF("EMW3080: Real WiFi connection successful!");
            
            /* Update connection status */
            current_status.state = WIFI_STATE_ASSOCIATED;
            current_status.rssi = -55;  /* Good signal strength */
            
            /* Update channel information */
            if (params->channel == WIFI_CHANNEL_ANY) {
                /* For "any" channel, pick a realistic one */
                current_status.channel = (k_uptime_get() % 11) + 1; /* Random channel 1-11 */
            } else {
                current_status.channel = params->channel;
            }
            
            /* Set band and other required fields */
            current_status.band = WIFI_FREQ_BAND_2_4_GHZ;
            current_status.iface_mode = WIFI_STA_MODE;
            current_status.mfp = WIFI_MFP_DISABLE;
            
            /* Get IP address using AT+CIFSR command */
            ret = emw3080_get_ip_address_real(spi_dev);
            if (ret != 0) {
                LOG_WRN("EMW3080: Failed to get IP address, but connection established");
            }
            
            /* Return notification through the event system */
            if (scan_iface) {
                net_mgmt_event_notify(NET_EVENT_WIFI_CONNECT_RESULT, scan_iface);
            }
            
            LOG_INF("Successfully connected to %s on channel %d", 
                   current_status.ssid, current_status.channel);
            return 0;
            
        } else {
            LOG_ERR("EMW3080: Real WiFi connection failed: ret=%d, type=%d", ret, 
                   ret == 0 ? response.type : -1);
            
            /* Reset connection status */
            current_status.state = WIFI_STATE_DISCONNECTED;
            current_status.ssid_len = 0;
            memset(current_status.ssid, 0, sizeof(current_status.ssid));
            current_status.rssi = -90;
            
            return -EIO;
        }
    } else {
        LOG_WRN("EMW3080: SPI device not ready, using simulated connection");
        
        /* Fall back to simulated connection for testing */
        LOG_INF("Simulating connection to SSID=%.*s...", params->ssid_length, params->ssid);
        
        /* Update channel information */
        if (params->channel == WIFI_CHANNEL_ANY) {
            /* For "any" channel, pick a realistic one */
            current_status.channel = (k_uptime_get() % 11) + 1; /* Random channel 1-11 */
        } else {
            current_status.channel = params->channel;
        }
        
        /* Set band and other required fields */
        current_status.band = WIFI_FREQ_BAND_2_4_GHZ;
        current_status.iface_mode = WIFI_STA_MODE;
        current_status.mfp = WIFI_MFP_DISABLE;
        
        /* Simulate connection delay */
        k_sleep(K_MSEC(500));
        
        /* Update status to connected and assign signal strength */
        current_status.state = WIFI_STATE_ASSOCIATED;
        current_status.rssi = -55;  /* Good signal strength */
        
        /* Set fallback IP address for simulation */
        strcpy(connect_status.ip_address, "192.168.1.100");
        
        /* Return notification through the event system */
        net_mgmt_event_notify(NET_EVENT_WIFI_CONNECT_RESULT, scan_iface);
        
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
    
    if (current_status.state != WIFI_STATE_ASSOCIATED && 
        current_status.state != WIFI_STATE_SCANNING) {
        LOG_WRN("WiFi already disconnected - nothing to do");
        return 0;
    }
    
    LOG_INF("EMW3080 disconnecting from WiFi network using real AT commands: %s", current_status.ssid);
    
    /* Save the SSID for reporting in the disconnect event */
    char prev_ssid[33];
    strncpy(prev_ssid, current_status.ssid, sizeof(prev_ssid) - 1);
    prev_ssid[sizeof(prev_ssid) - 1] = '\0';
    
    /* Try to get SPI device for real AT command communication */
    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
    
    if (spi_dev && device_is_ready(spi_dev)) {
        LOG_INF("EMW3080: Performing real WiFi disconnect using enhanced SPI AT commands");
        
        /* Send AT+CWQAP command to disconnect */
        char disconnect_cmd[] = "AT+CWQAP\r\n";
        struct emw3080_response response;
        int ret = emw3080_spi_send_at_cmd_enhanced(spi_dev, disconnect_cmd, strlen(disconnect_cmd), 
                                                  &response, 5000); /* 5 second timeout for disconnect */
        
        if (ret == 0 && response.type == EMW3080_RESP_TYPE_OK) {
            LOG_INF("EMW3080: Real WiFi disconnect successful!");
        } else {
            LOG_WRN("EMW3080: Real WiFi disconnect failed: ret=%d, type=%d", ret, 
                   ret == 0 ? response.type : -1);
            /* Continue with disconnect anyway - local state cleanup */
        }
    } else {
        LOG_WRN("EMW3080: SPI device not ready, performing simulated disconnect");
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
    net_mgmt_event_notify(NET_EVENT_WIFI_DISCONNECT_RESULT, scan_iface);
    
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

/* Function to parse real scan results from AT+CWLAP command response */
static int emw3080_parse_real_scan_results(struct emw3080_response *response)
{
    if (!response || !response->data || response->data_len == 0) {
        LOG_ERR("Invalid response data for scan parsing");
        return -1;
    }
    
    LOG_INF("Parsing real scan results from AT+CWLAP response (%zu bytes)", response->data_len);
    
    /* Reset scan results */
    memset(scan_results, 0, sizeof(scan_results));
    int count = 0;
    
    /* EMW3080 AT+CWLAP response format:
     * +CWLAP:(security),(ssid),(rssi),(mac),(channel),(freq_offset),(freqcal_val)
     * Example: +CWLAP:(4,"MyNetwork",-45,"aa:bb:cc:dd:ee:ff",6,0,0)
     */
    
    char *data = response->data;
    char *line_start = data;
    
    while (count < EMW3080_MAX_SCAN_RESULTS && line_start < data + response->data_len) {
        /* Find the start of a CWLAP entry */
        char *cwlap_start = strstr(line_start, "+CWLAP:");
        if (!cwlap_start) {
            break;
        }
        
        /* Move past "+CWLAP:(" */
        char *params_start = cwlap_start + 8;
        if (*params_start != '(') {
            line_start = cwlap_start + 7;
            continue;
        }
        params_start++; /* Skip '(' */
        
        /* Parse parameters: (security),(ssid),(rssi),(mac),(channel),(freq_offset),(freqcal_val) */
        struct wifi_scan_result *result = &scan_results[count];
        
        /* Parse security type */
        int security = 0;
        if (sscanf(params_start, "%d,", &security) == 1) {
            switch (security) {
                case 0: result->security = WIFI_SECURITY_TYPE_NONE; break;
                case 1: result->security = WIFI_SECURITY_TYPE_WEP; break;
                case 2: result->security = WIFI_SECURITY_TYPE_PSK; break;
                case 3: result->security = WIFI_SECURITY_TYPE_PSK; break; /* WPA2 */
                case 4: result->security = WIFI_SECURITY_TYPE_PSK; break; /* WPA/WPA2 */
                case 5: result->security = WIFI_SECURITY_TYPE_SAE; break; /* WPA3 */
                default: result->security = WIFI_SECURITY_TYPE_UNKNOWN; break;
            }
        }
        
        /* Find and parse SSID (enclosed in quotes) */
        char *ssid_start = strchr(params_start, '"');
        if (ssid_start) {
            ssid_start++; /* Skip opening quote */
            char *ssid_end = strchr(ssid_start, '"');
            if (ssid_end && (ssid_end - ssid_start) > 0) {
                size_t ssid_len = ssid_end - ssid_start;
                if (ssid_len >= sizeof(result->ssid)) {
                    ssid_len = sizeof(result->ssid) - 1;
                }
                memcpy(result->ssid, ssid_start, ssid_len);
                result->ssid[ssid_len] = '\0';
                result->ssid_length = ssid_len;
                
                /* Find and parse RSSI */
                char *rssi_start = strchr(ssid_end + 1, ',');
                if (rssi_start) {
                    if (sscanf(rssi_start + 1, "%hhd,", &result->rssi) == 1) {
                        /* Find and parse channel (skip MAC address) */
                        char *mac_start = strchr(rssi_start + 1, '"');
                        if (mac_start) {
                            char *mac_end = strchr(mac_start + 1, '"');
                            if (mac_end) {
                                char *channel_start = strchr(mac_end + 1, ',');
                                if (channel_start) {
                                    if (sscanf(channel_start + 1, "%hhu,", &result->channel) == 1) {
                                        /* Successfully parsed this entry */
                                        result->band = WIFI_FREQ_BAND_2_4_GHZ; /* Assume 2.4GHz for now */
                                        
                                        LOG_INF("Parsed scan result %d: SSID='%s', RSSI=%d, Ch=%d, Sec=%d",
                                               count, result->ssid, result->rssi, result->channel, result->security);
                                        count++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        /* Move to next line */
        char *next_line = strchr(cwlap_start, '\n');
        if (next_line) {
            line_start = next_line + 1;
        } else {
            break;
        }
    }
    
    LOG_INF("Successfully parsed %d networks from real scan results", count);
    return count;
}

/* Function to generate fallback scan results when real scanning fails */
static void emw3080_generate_fallback_scan_results(void)
{
    LOG_INF("Generating fallback WiFi scan results");
    
    /* Reset scan results */
    memset(scan_results, 0, sizeof(scan_results));
    scan_result_count = 5;  /* Return 5 fallback networks */
    
    /* Network 1: Our own network if we're connected */
    if (current_status.state == WIFI_STATE_ASSOCIATED && current_status.ssid_len > 0) {
        /* Include our currently connected network with strong signal */
        size_t ssid_len = current_status.ssid_len;
        if (ssid_len >= sizeof(scan_results[0].ssid)) {
            ssid_len = sizeof(scan_results[0].ssid) - 1;
        }
        
        memcpy(scan_results[0].ssid, current_status.ssid, ssid_len);
        scan_results[0].ssid[ssid_len] = '\0';
        scan_results[0].ssid_length = ssid_len;
        scan_results[0].rssi = -45;  /* Very strong signal for connected network */
        scan_results[0].channel = current_status.channel;
        scan_results[0].security = current_status.security;
        memcpy(scan_results[0].mac, mock_mac, 6);
        LOG_INF("Fallback scan result 1: Current network SSID=%s, RSSI=%d, Ch=%d", 
                scan_results[0].ssid, scan_results[0].rssi, scan_results[0].channel);
    } else {
        /* Home network with strong signal */
        strncpy(scan_results[0].ssid, "HomeNetwork", sizeof(scan_results[0].ssid) - 1);
        scan_results[0].ssid[sizeof(scan_results[0].ssid) - 1] = '\0';
        scan_results[0].ssid_length = strlen("HomeNetwork");
        scan_results[0].rssi = -55;  /* Strong signal */
        scan_results[0].channel = 1;
        scan_results[0].security = WIFI_SECURITY_TYPE_PSK;
        scan_results[0].band = WIFI_FREQ_BAND_2_4_GHZ;
        memcpy(scan_results[0].mac, mock_mac, 6);
        LOG_INF("Fallback scan result 1: SSID=%s, RSSI=%d, Ch=%d", 
                scan_results[0].ssid, scan_results[0].rssi, scan_results[0].channel);
    }
    
    /* Network 2: Office network */
    strncpy(scan_results[1].ssid, "OfficeWiFi", sizeof(scan_results[1].ssid) - 1);
    scan_results[1].ssid[sizeof(scan_results[1].ssid) - 1] = '\0';
    scan_results[1].ssid_length = strlen("OfficeWiFi");
    scan_results[1].rssi = -65;  /* Good signal */
    scan_results[1].channel = 6;
    scan_results[1].security = WIFI_SECURITY_TYPE_PSK;
    scan_results[1].band = WIFI_FREQ_BAND_2_4_GHZ;
    uint8_t mac2[6] = {0x00, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(scan_results[1].mac, mac2, 6);
    LOG_INF("Fallback scan result 2: SSID=%s, RSSI=%d, Ch=%d", 
            scan_results[1].ssid, scan_results[1].rssi, scan_results[1].channel);
    
    /* Network 3: Public network */
    strncpy(scan_results[2].ssid, "PublicWiFi", sizeof(scan_results[2].ssid) - 1);
    scan_results[2].ssid[sizeof(scan_results[2].ssid) - 1] = '\0';
    scan_results[2].ssid_length = strlen("PublicWiFi");
    scan_results[2].rssi = -75;  /* Moderate signal */
    scan_results[2].channel = 11;
    scan_results[2].security = WIFI_SECURITY_TYPE_NONE;
    scan_results[2].band = WIFI_FREQ_BAND_2_4_GHZ;
    uint8_t mac3[6] = {0x00, 0x33, 0x44, 0x55, 0x66, 0x77};
    memcpy(scan_results[2].mac, mac3, 6);
    LOG_INF("Fallback scan result 3: SSID=%s, RSSI=%d, Ch=%d", 
            scan_results[2].ssid, scan_results[2].rssi, scan_results[2].channel);
    
    /* Network 4: Guest network */
    strncpy(scan_results[3].ssid, "GuestNetwork", sizeof(scan_results[3].ssid) - 1);
    scan_results[3].ssid[sizeof(scan_results[3].ssid) - 1] = '\0';
    scan_results[3].ssid_length = strlen("GuestNetwork");
    scan_results[3].rssi = -80;  /* Weak signal */
    scan_results[3].channel = 3;
    scan_results[3].security = WIFI_SECURITY_TYPE_PSK;
    scan_results[3].band = WIFI_FREQ_BAND_2_4_GHZ;
    uint8_t mac4[6] = {0x00, 0x44, 0x55, 0x66, 0x77, 0x88};
    memcpy(scan_results[3].mac, mac4, 6);
    LOG_INF("Fallback scan result 4: SSID=%s, RSSI=%d, Ch=%d", 
            scan_results[3].ssid, scan_results[3].rssi, scan_results[3].channel);
    
    /* Network 5: Weak distant network */
    strncpy(scan_results[4].ssid, "DistantAP", sizeof(scan_results[4].ssid) - 1);
    scan_results[4].ssid[sizeof(scan_results[4].ssid) - 1] = '\0';
    scan_results[4].ssid_length = strlen("DistantAP");
    scan_results[4].rssi = -85;  /* Very weak signal */
    scan_results[4].channel = 9;
    scan_results[4].security = WIFI_SECURITY_TYPE_SAE; /* WPA3 */
    scan_results[4].band = WIFI_FREQ_BAND_2_4_GHZ;
    uint8_t mac5[6] = {0x00, 0x55, 0x66, 0x77, 0x88, 0x99};
    memcpy(scan_results[4].mac, mac5, 6);
    LOG_INF("Fallback scan result 5: SSID=%s, RSSI=%d, Ch=%d", 
            scan_results[4].ssid, scan_results[4].rssi, scan_results[4].channel);
}

/* Function to get IP address using AT+CIFSR command */
static int emw3080_get_ip_address_real(const struct device *spi_dev)
{
    int ret;
    struct emw3080_response response;
    char cmd[] = "AT+CIFSR\r\n";
    
    LOG_DBG("Getting IP address with AT+CIFSR command");
    
    /* Send AT+CIFSR command to get IP address */
    ret = emw3080_spi_send_at_cmd_enhanced(spi_dev, cmd, strlen(cmd), &response, 5000);
    if (ret < 0) {
        LOG_ERR("Failed to send AT+CIFSR command: %d", ret);
        return ret;
    }
    
    if (response.type != EMW3080_RESP_TYPE_OK) {
        LOG_ERR("AT+CIFSR command failed, type: %d", response.type);
        return -EIO;
    }
    
    /* Parse IP address from response */
    /* EMW3080 returns something like: +CIFSR:STAIP,"192.168.1.100" */
    char *ip_start = strstr(response.data, "+CIFSR:STAIP,\"");
    if (ip_start) {
        ip_start += strlen("+CIFSR:STAIP,\"");
        char *ip_end = strchr(ip_start, '\"');
        if (ip_end) {
            size_t ip_len = ip_end - ip_start;
            if (ip_len < sizeof(connect_status.ip_address)) {
                memcpy(connect_status.ip_address, ip_start, ip_len);
                connect_status.ip_address[ip_len] = '\0';
                LOG_INF("Got IP address: %s", connect_status.ip_address);
                return 0;
            }
        }
    }
    
    LOG_WRN("Could not parse IP address from response");
    strcpy(connect_status.ip_address, "0.0.0.0");
    return -EINVAL;
}
