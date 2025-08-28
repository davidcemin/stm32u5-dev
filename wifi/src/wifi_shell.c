#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/offloaded_netdev.h>
#include <string.h>

/* Include the EMW3080 management header directly to access its API */
#include "../drivers/wifi/emw3080/emw3080_mgmt.h"

/* Helper function to get the EMW3080 device directly */
static const struct device *get_emw3080_device(void)
{
    /* Look for a device with EMW3080 in the name */
    int i = 0;
    
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp = net_if_get_by_index(i);
        if (!tmp) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(tmp);
        if (!dev) {
            continue;
        }
        
        if (dev->name && strstr(dev->name, "EMW3080") != NULL) {
            return dev;  /* Return the device directly */
        }
    }
    
    return NULL;  /* No EMW3080 device found */
}

/* Helper function to get Wi-Fi interface - ULTRA SAFE IMPLEMENTATION */
static struct net_if *get_wifi_iface(void)
{
    /* ULTRA SAFE APPROACH - Minimal assumptions, simplest possible code */
    int i = 0;
    
    /* SAFETY FIRST: Only look for EMW3080 in device name - most reliable approach */
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp = net_if_get_by_index(i);
        if (!tmp) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(tmp);
        if (!dev) {
            continue;
        }
        
        if (dev->name && strstr(dev->name, "EMW3080") != NULL) {
            return tmp;  /* Return immediately when we find a match */
        }
    }
    
    /* If we didn't find EMW3080 by name, just return NULL */
    /* Better to fail than to return a wrong interface */
    return NULL;
}

/* Wi-Fi scan command - ULTRA SAFE POLLING IMPLEMENTATION */
static int cmd_wifi_scan(const struct shell *sh, size_t argc, char *argv[])
{
    shell_fprintf(sh, SHELL_NORMAL, "Starting Wi-Fi scan using ULTRA SAFE polling method...\n");
    
    /* Get the EMW3080 device directly */
    const struct device *emw_dev = get_emw3080_device();
    if (!emw_dev) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 device found\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Found EMW3080 device: %s\n", 
                 emw_dev->name ? emw_dev->name : "unnamed");
    
    /* Use empty scan params structure */
    struct wifi_scan_params scan_params = {0};
    
    /* Call the EMW3080 management API directly - passing NULL for callback */
    shell_fprintf(sh, SHELL_NORMAL, "Starting scan (no callback)...\n");
    int err = emw3080_mgmt_scan(emw_dev, &scan_params, NULL);
    
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Scan failed: %d\n", err);
        return -EIO;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Scan started. Waiting for results (1 second max)...\n");
    
    /* Poll for scan results - safer than callbacks */
    int retries = 20;  /* 20 * 50ms = 1 second max wait time */
    while (!emw3080_mgmt_scan_results_ready() && retries > 0) {
        /* Wait a bit */
        k_sleep(K_MSEC(50));
        retries--;
    }
    
    if (!emw3080_mgmt_scan_results_ready()) {
        shell_fprintf(sh, SHELL_ERROR, "Scan timed out waiting for results\n");
        return -ETIMEDOUT;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Scan results ready - retrieving...\n");
    shell_fprintf(sh, SHELL_NORMAL, "\n====== WIFI NETWORKS ======\n");
    
    /* Get the results directly */
    int max_networks = 20;  /* Increased from 10 to capture more networks */
    int count = 0;
    
    /* Allocate a larger buffer to make sure we capture all networks */
    struct wifi_scan_result results[max_networks];
    
    shell_fprintf(sh, SHELL_NORMAL, "Retrieving up to %d networks...\n", max_networks);
    
    /* Get results directly - we'll use the count returned by the function */
    err = emw3080_mgmt_get_scan_results(results, max_networks, &count);
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Failed to get scan results: %d\n", err);
        return -EIO;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Retrieved %d networks\n", count);
    
    if (count == 0) {
        shell_fprintf(sh, SHELL_NORMAL, "No networks found\n");
        return 0;
    }
    
    /* Display the results safely */
    for (int i = 0; i < count; i++) {
        char safe_ssid[33] = {0};
        
        shell_fprintf(sh, SHELL_NORMAL, "-------------------------\n");
        
        /* Safely handle SSID */
        if (results[i].ssid_length == 0) {
            shell_fprintf(sh, SHELL_NORMAL, "Network %d: <hidden>\n", i+1);
        } else {
            /* Copy to local buffer for safety */
            size_t copy_len = results[i].ssid_length;
            if (copy_len > 32) {
                copy_len = 32;
            }
            memcpy(safe_ssid, results[i].ssid, copy_len);
            safe_ssid[copy_len] = '\0';
            
            shell_fprintf(sh, SHELL_NORMAL, "Network %d: %s\n", i+1, safe_ssid);
        }
        
        /* Display other details */
        shell_fprintf(sh, SHELL_NORMAL, "  Signal: %d dBm\n", results[i].rssi);
        shell_fprintf(sh, SHELL_NORMAL, "  Channel: %u\n", results[i].channel);
        
        /* Translate security type to string */
        const char *security;
        switch (results[i].security) {
            case WIFI_SECURITY_TYPE_NONE:
                security = "Open";
                break;
            case WIFI_SECURITY_TYPE_PSK:
                security = "WPA2-PSK";
                break;
            case WIFI_SECURITY_TYPE_PSK_SHA256:
                security = "WPA2-PSK-SHA256";
                break;
            case WIFI_SECURITY_TYPE_SAE:
                security = "WPA3-SAE";
                break;
            default:
                security = "Unknown";
        }
        shell_fprintf(sh, SHELL_NORMAL, "  Security: %s\n", security);
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "-------------------------\n");
    shell_fprintf(sh, SHELL_NORMAL, "%d networks found\n", count);
    return 0;
}

/* Wi-Fi connect command - ULTRA SAFE DIRECT IMPLEMENTATION */
static int cmd_wifi_connect(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 3) {
        shell_fprintf(sh, SHELL_ERROR,
                      "Usage: wifi connect <SSID> <PSK> [security_type]\n");
        shell_fprintf(sh, SHELL_NORMAL,
                      "Security types: 0=Open, 1=WPA2-PSK, 2=WPA2-PSK-SHA256, 3=WPA3-SAE\n");
        return -EINVAL;
    }

    /* Get the EMW3080 device directly - safer than using network interface */
    const struct device *emw_dev = get_emw3080_device();
    if (!emw_dev) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 device found\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Found EMW3080 device: %s\n", 
                 emw_dev->name ? emw_dev->name : "unnamed");
    
    /* Create a safe local copy of the SSID and PSK with proper bounds checking */
    char safe_ssid[33] = {0};
    char safe_psk[65] = {0};
    
    size_t ssid_len = strlen(argv[1]);
    size_t psk_len = strlen(argv[2]);
    
    /* Validate SSID length */
    if (ssid_len == 0 || ssid_len > 32) {
        shell_fprintf(sh, SHELL_ERROR, "Invalid SSID length: %zu (must be 1-32 characters)\n", ssid_len);
        return -EINVAL;
    }
    
    /* Validate PSK length - empty is allowed for open networks */
    if (psk_len > 64) {
        shell_fprintf(sh, SHELL_ERROR, "Invalid PSK length: %zu (must be 0-64 characters)\n", psk_len);
        return -EINVAL;
    }
    
    /* Copy the SSID and PSK to safe buffers */
    strncpy(safe_ssid, argv[1], 32);
    safe_ssid[32] = '\0';  /* Ensure null termination */
    
    strncpy(safe_psk, argv[2], 64);
    safe_psk[64] = '\0';  /* Ensure null termination */
    
    /* Determine security type */
    enum wifi_security_type security = WIFI_SECURITY_TYPE_PSK;  /* Default to WPA2-PSK */
    
    if (argc >= 4) {
        /* User specified security type */
        int sec_type = 1; /* Default to PSK */
        
        /* Convert string to integer safely */
        if (argv[3][0] >= '0' && argv[3][0] <= '9') {
            sec_type = argv[3][0] - '0';
        }
        
        switch (sec_type) {
            case WIFI_SECURITY_TYPE_NONE:
                security = WIFI_SECURITY_TYPE_NONE;
                break;
            case WIFI_SECURITY_TYPE_PSK:
                security = WIFI_SECURITY_TYPE_PSK;
                break;
            case WIFI_SECURITY_TYPE_PSK_SHA256:
                security = WIFI_SECURITY_TYPE_PSK_SHA256;
                break;
            case WIFI_SECURITY_TYPE_SAE:
                security = WIFI_SECURITY_TYPE_SAE;
                break;
            default:
                shell_fprintf(sh, SHELL_WARNING, "Unrecognized security type %d, defaulting to WPA2-PSK\n", 
                             sec_type);
                security = WIFI_SECURITY_TYPE_PSK;
        }
    } else if (psk_len == 0) {
        /* Infer security type from PSK length */
        security = WIFI_SECURITY_TYPE_NONE;
        shell_fprintf(sh, SHELL_NORMAL, "No PSK provided, assuming open network\n");
    }
    
    /* Prepare the connection parameters */
    struct wifi_connect_req_params params = { 0 };
    
    params.ssid = safe_ssid;
    params.ssid_length = ssid_len;
    
    params.psk = safe_psk;
    params.psk_length = psk_len;
    
    params.channel = WIFI_CHANNEL_ANY;
    params.security = security;
    
    /* Display connection attempt details */
    shell_fprintf(sh, SHELL_NORMAL, "Connecting to SSID: %s\n", safe_ssid);
    shell_fprintf(sh, SHELL_NORMAL, "Security type: %s\n", 
                security == WIFI_SECURITY_TYPE_NONE ? "Open" :
                security == WIFI_SECURITY_TYPE_PSK ? "WPA2-PSK" :
                security == WIFI_SECURITY_TYPE_PSK_SHA256 ? "WPA2-PSK-SHA256" :
                security == WIFI_SECURITY_TYPE_SAE ? "WPA3-SAE" : "Unknown");
    
    /* Call the EMW3080 management API directly */
    shell_fprintf(sh, SHELL_NORMAL, "Initiating connection using direct EMW3080 API...\n");
    int err = emw3080_mgmt_connect(emw_dev, &params);
    
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Connection failed with error: %d\n", err);
        return -EIO;
    }
    
    /* Check if already connected first */
    struct wifi_iface_status initial_status = {0};
    if (emw3080_mgmt_get_status(emw_dev, &initial_status) == 0) {
        /* If already connected to the requested network, report success immediately */
        if (initial_status.ssid_len > 0 && 
            strncmp(initial_status.ssid, safe_ssid, initial_status.ssid_len) == 0) {
            shell_fprintf(sh, SHELL_NORMAL, "Already connected to %s!\n", safe_ssid);
            
            /* Display connection details */
            shell_fprintf(sh, SHELL_NORMAL, "\nConnection Details:\n");
            shell_fprintf(sh, SHELL_NORMAL, "-------------------\n");
            shell_fprintf(sh, SHELL_NORMAL, "State: Connected\n");
            shell_fprintf(sh, SHELL_NORMAL, "RSSI: %d dBm\n", initial_status.rssi);
            shell_fprintf(sh, SHELL_NORMAL, "Channel: %u\n", initial_status.channel);
            shell_fprintf(sh, SHELL_NORMAL, "\nUse 'net ipv4' to check IP configuration\n");
            return 0;
        } else if (initial_status.ssid_len > 0) {
            /* Already connected to a different network */
            char current_ssid[33] = {0};
            strncpy(current_ssid, initial_status.ssid, 
                   initial_status.ssid_len > 32 ? 32 : initial_status.ssid_len);
            
            shell_fprintf(sh, SHELL_NORMAL, "Currently connected to %s. Reconnecting to %s...\n", 
                         current_ssid, safe_ssid);
        }
    }
    
    /* Poll for connection status */
    shell_fprintf(sh, SHELL_NORMAL, "Connection request sent. Waiting for connection (10 seconds max)...\n");
    
    int retries = 100;  /* 100 * 100ms = 10 second max wait time */
    bool is_connected = false;
    struct wifi_iface_status status = {0};
    
    while (retries > 0) {
        /* Check connection status */
        if (emw3080_mgmt_get_status(emw_dev, &status) == 0) {
            /* Check for any connected state (not just WIFI_STATE_COMPLETED) */
            if ((status.state == WIFI_STATE_COMPLETED) || 
                (status.ssid_len > 0 && strncmp(status.ssid, safe_ssid, status.ssid_len) == 0)) {
                is_connected = true;
                break;
            }
        }
        
        /* Wait a bit */
        k_sleep(K_MSEC(100));
        retries--;
        
        if (retries % 10 == 0) {
            /* Print status every second */
            shell_fprintf(sh, SHELL_NORMAL, "Still connecting... (%d seconds remaining)\n", retries / 10);
        }
    }
    
    if (is_connected) {
        shell_fprintf(sh, SHELL_NORMAL, "Successfully connected to %s!\n", safe_ssid);
        
        /* Display connection details */
        shell_fprintf(sh, SHELL_NORMAL, "\nConnection Details:\n");
        shell_fprintf(sh, SHELL_NORMAL, "-------------------\n");
        shell_fprintf(sh, SHELL_NORMAL, "State: Connected\n");
        shell_fprintf(sh, SHELL_NORMAL, "RSSI: %d dBm\n", status.rssi);
        shell_fprintf(sh, SHELL_NORMAL, "Channel: %u\n", status.channel);
        shell_fprintf(sh, SHELL_NORMAL, "\nUse 'net ipv4' to check IP configuration\n");
        return 0;
    }
    
    shell_fprintf(sh, SHELL_ERROR, "Connection timed out after 10 seconds\n");
    return -ETIMEDOUT;
}

/* Wi-Fi disconnect command - ULTRA SAFE DIRECT IMPLEMENTATION */
static int cmd_wifi_disconnect(const struct shell *sh, size_t argc, char *argv[])
{
    /* Get the EMW3080 device directly - safer than using network interface */
    const struct device *emw_dev = get_emw3080_device();
    if (!emw_dev) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 device found\n");
        return -ENODEV;
    }

    /* Check current connection status first */
    struct wifi_iface_status status = {0};
    bool is_connected = false;
    char current_ssid[33] = {0};
    
    if (emw3080_mgmt_get_status(emw_dev, &status) == 0) {
        if (status.ssid_len > 0) {
            is_connected = true;
            /* Copy SSID to a safe buffer */
            strncpy(current_ssid, status.ssid, 
                   status.ssid_len > 32 ? 32 : status.ssid_len);
            current_ssid[32] = '\0';
        }
    }
    
    if (!is_connected) {
        shell_fprintf(sh, SHELL_NORMAL, "Not currently connected to any WiFi network\n");
        return 0;
    }

    shell_fprintf(sh, SHELL_NORMAL, "Disconnecting from WiFi network '%s' using direct EMW3080 API...\n", 
                 current_ssid);
    
    /* Call the EMW3080 management API directly */
    int err = emw3080_mgmt_disconnect(emw_dev);
    
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Disconnect failed with error: %d\n", err);
        return -EIO;
    }
    
    /* Poll for disconnection status */
    shell_fprintf(sh, SHELL_NORMAL, "Disconnect request sent. Waiting for confirmation (3 seconds max)...\n");
    
    int retries = 30;  /* 30 * 100ms = 3 second max wait time */
    bool is_disconnected = false;
    
    while (retries > 0) {
        /* Check connection status */
        struct wifi_iface_status status = {0};
        if (emw3080_mgmt_get_status(emw_dev, &status) == 0) {
            if (status.ssid_len == 0) {  /* Check for empty SSID = not connected */
                is_disconnected = true;
                break;
            }
        }
        
        /* Wait a bit */
        k_sleep(K_MSEC(100));
        retries--;
    }
    
    if (is_disconnected) {
        shell_fprintf(sh, SHELL_NORMAL, "Successfully disconnected from WiFi network '%s'\n", current_ssid);
        return 0;
    } else {
        shell_fprintf(sh, SHELL_WARNING, "Disconnect request sent, but device still appears to be connected\n");
        shell_fprintf(sh, SHELL_WARNING, "You may need to check device status manually\n");
        return -EIO;
    }
}

/* Wi-Fi status command - ULTRA SAFE DIRECT IMPLEMENTATION */
static int cmd_wifi_status(const struct shell *sh, size_t argc, char *argv[])
{
    /* Get the EMW3080 device directly - safer than using network interface */
    const struct device *emw_dev = get_emw3080_device();
    if (!emw_dev) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 device found\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Wi-Fi Interface Status Report\n");
    shell_fprintf(sh, SHELL_NORMAL, "==========================\n");
    shell_fprintf(sh, SHELL_NORMAL, "Device: %s\n", emw_dev->name ? emw_dev->name : "<unnamed>");
    
    /* Get WiFi interface by device */
    struct net_if *iface = NULL;
    int i;
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp = net_if_get_by_index(i);
        if (!tmp) {
            continue;
        }
        
        if (net_if_get_device(tmp) == emw_dev) {
            iface = tmp;
            break;
        }
    }
    
    if (!iface) {
        shell_fprintf(sh, SHELL_WARNING, "EMW3080 device found but no associated network interface\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Interface: #%d\n", net_if_get_by_iface(iface));
    shell_fprintf(sh, SHELL_NORMAL, "Interface Status: %s\n", 
                net_if_is_up(iface) ? "UP" : "DOWN");
    
    /* Now get the WiFi connection status using direct EMW3080 API */
    shell_fprintf(sh, SHELL_NORMAL, "\nChecking WiFi connection status...\n");
    
    struct wifi_iface_status status = {0};
    int err = emw3080_mgmt_get_status(emw_dev, &status);
    
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Failed to get WiFi status: %d\n", err);
        shell_fprintf(sh, SHELL_NORMAL, "Try 'net iface' for basic interface information\n");
        return -EIO;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "\nWiFi Connection Details:\n");
    shell_fprintf(sh, SHELL_NORMAL, "=======================\n");
    
    /* Display connection status */
    if (status.ssid_len > 0) {
        char safe_ssid[33] = {0};
        strncpy(safe_ssid, status.ssid, 
               status.ssid_len > 32 ? 32 : status.ssid_len);
        safe_ssid[32] = '\0';
        
        shell_fprintf(sh, SHELL_NORMAL, "Status: Connected\n");
        shell_fprintf(sh, SHELL_NORMAL, "Network: %s\n", safe_ssid);
        shell_fprintf(sh, SHELL_NORMAL, "Channel: %u\n", status.channel);
        shell_fprintf(sh, SHELL_NORMAL, "Signal Strength: %d dBm\n", status.rssi);
        
        /* Display security type if available */
        const char *security;
        switch (status.security) {
            case WIFI_SECURITY_TYPE_NONE:
                security = "Open";
                break;
            case WIFI_SECURITY_TYPE_PSK:
                security = "WPA2-PSK";
                break;
            case WIFI_SECURITY_TYPE_PSK_SHA256:
                security = "WPA2-PSK-SHA256";
                break;
            case WIFI_SECURITY_TYPE_SAE:
                security = "WPA3-SAE";
                break;
            default:
                security = "Unknown";
        }
        shell_fprintf(sh, SHELL_NORMAL, "Security: %s\n", security);
        
        /* Display MAC address if available and valid */
        if (status.bssid[0] || status.bssid[1] || status.bssid[2] || 
            status.bssid[3] || status.bssid[4] || status.bssid[5]) {
            shell_fprintf(sh, SHELL_NORMAL, "AP MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                        status.bssid[0], status.bssid[1], status.bssid[2],
                        status.bssid[3], status.bssid[4], status.bssid[5]);
        }
    } else {
        shell_fprintf(sh, SHELL_NORMAL, "Status: Not connected\n");
    }
    
    /* Display IP configuration information */
    shell_fprintf(sh, SHELL_NORMAL, "\nIP Configuration:\n");
    shell_fprintf(sh, SHELL_NORMAL, "---------------\n");
    shell_fprintf(sh, SHELL_NORMAL, "Use 'net iface' or 'net ipv4' for detailed IP configuration\n");
    
    return 0;
}

/* Network interface up/down command */
static int cmd_wifi_power(const struct shell *sh, size_t argc, char *argv[])
{
    struct net_if *iface = get_wifi_iface();
    
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
        return -ENODEV;
    }
    
    if (argc < 2) {
        shell_fprintf(sh, SHELL_ERROR, "Usage: wifi power <on|off>\n");
        return -EINVAL;
    }

    if (strcmp(argv[1], "on") == 0) {
        net_if_up(iface);
        shell_fprintf(sh, SHELL_NORMAL, "Wi-Fi interface powered ON\n");
    } else if (strcmp(argv[1], "off") == 0) {
        net_if_down(iface);
        shell_fprintf(sh, SHELL_NORMAL, "Wi-Fi interface powered OFF\n");
    } else {
        shell_fprintf(sh, SHELL_ERROR, "Invalid argument: %s\n", argv[1]);
        return -EINVAL;
    }

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(wifi_cmds,
    SHELL_CMD(scan, NULL, "Scan for Wi-Fi networks", cmd_wifi_scan),
    SHELL_CMD(connect, NULL, "Connect: wifi connect <SSID> <PSK>", cmd_wifi_connect),
    SHELL_CMD(disconnect, NULL, "Disconnect from Wi-Fi network", cmd_wifi_disconnect),
    SHELL_CMD(status, NULL, "Show Wi-Fi interface status", cmd_wifi_status),
    SHELL_CMD(power, NULL, "Power on/off Wi-Fi: wifi power <on|off>", cmd_wifi_power),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(wifi, &wifi_cmds, "Wi-Fi commands", NULL);
