#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/offloaded_netdev.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_ip.h>
#include <string.h>

/* Include the EMW3080 management header directly to access its API */
#include "../drivers/wifi/emw3080/emw3080_mgmt.h"

/* Include MIPC protocol support */
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>

#include "../drivers/wifi/emw3080/emw3080_mipc.h"
#include "../drivers/wifi/emw3080/emw3080_mipc_spi.h"

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

/* DHCP configuration command */
static int cmd_wifi_dhcp(const struct shell *sh, size_t argc, char *argv[])
{
    /* Get the EMW3080 interface directly - safer than using network interface API */
    struct net_if *iface = NULL;
    const struct device *emw_dev = get_emw3080_device();
    
    if (!emw_dev) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 device found\n");
        return -ENODEV;
    }
    
    /* Find network interface for the EMW3080 device */
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
        shell_fprintf(sh, SHELL_ERROR, "No network interface found for EMW3080\n");
        return -ENODEV;
    }
    
    if (argc < 2) {
        shell_fprintf(sh, SHELL_ERROR, "Usage: wifi dhcp <start|stop>\n");
        return -EINVAL;
    }

    if (strcmp(argv[1], "start") == 0) {
        shell_fprintf(sh, SHELL_NORMAL, "Starting DHCP for WiFi interface...\n");
        
        /* Reset any existing IPv4 config first */
        
        /* Clear any existing IPv4 addresses */
        struct in_addr *addr4 = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        if (addr4) {
            /* Remove the address */
            net_if_ipv4_addr_rm(iface, addr4);
        }
        
        /* Start DHCP */
        net_dhcpv4_start(iface);
        
        shell_fprintf(sh, SHELL_NORMAL, "DHCP client started. Use 'net ipv4' to check IP address\n");
        shell_fprintf(sh, SHELL_NORMAL, "Waiting for IP address assignment (may take a few seconds)...\n");
    } else if (strcmp(argv[1], "stop") == 0) {
        shell_fprintf(sh, SHELL_NORMAL, "Stopping DHCP for WiFi interface...\n");
        
        /* Stop DHCP */
        net_dhcpv4_stop(iface);
        
        shell_fprintf(sh, SHELL_NORMAL, "DHCP client stopped\n");
    } else {
        shell_fprintf(sh, SHELL_ERROR, "Invalid argument: %s\n", argv[1]);
        shell_fprintf(sh, SHELL_ERROR, "Usage: wifi dhcp <start|stop>\n");
        return -EINVAL;
    }

    return 0;
}

/* Add IP configuration command - direct IPv4 configuration */
static int cmd_wifi_ip(const struct shell *sh, size_t argc, char *argv[])
{
    struct net_if *iface = NULL;
    const struct device *emw_dev = get_emw3080_device();
    
    if (!emw_dev) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 device found\n");
        return -ENODEV;
    }
    
    /* Find network interface for the EMW3080 device */
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
        shell_fprintf(sh, SHELL_ERROR, "No network interface found for EMW3080\n");
        return -ENODEV;
    }
    
    if (argc < 2) {
        shell_fprintf(sh, SHELL_ERROR, 
                     "Usage: wifi ip set <ip_addr> <netmask> <gateway>\n");
        shell_fprintf(sh, SHELL_ERROR, 
                     "Example: wifi ip set 192.168.1.100 255.255.255.0 192.168.1.1\n");
        return -EINVAL;
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 5) {
            shell_fprintf(sh, SHELL_ERROR, 
                         "Usage: wifi ip set <ip_addr> <netmask> <gateway>\n");
            return -EINVAL;
        }
        
        /* Stop any running DHCP first */
        net_dhcpv4_stop(iface);
        
        /* Parse IP address */
        struct in_addr addr;
        struct in_addr netmask;
        struct in_addr gateway;
        
        if (net_addr_pton(AF_INET, argv[2], &addr) < 0) {
            shell_fprintf(sh, SHELL_ERROR, "Invalid IP address: %s\n", argv[2]);
            return -EINVAL;
        }
        
        if (net_addr_pton(AF_INET, argv[3], &netmask) < 0) {
            shell_fprintf(sh, SHELL_ERROR, "Invalid netmask: %s\n", argv[3]);
            return -EINVAL;
        }
        
        if (net_addr_pton(AF_INET, argv[4], &gateway) < 0) {
            shell_fprintf(sh, SHELL_ERROR, "Invalid gateway: %s\n", argv[4]);
            return -EINVAL;
        }
        
        /* Remove any existing IPv4 addresses */
        
        /* Clear any existing IPv4 addresses */
        struct in_addr *addr4 = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
        if (addr4) {
            /* Remove the address */
            net_if_ipv4_addr_rm(iface, addr4);
        }
        
        /* Set new IP address */
        net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
        
        /* Set netmask */
        net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);
        
        /* Set gateway */
        net_if_ipv4_set_gw(iface, &gateway);
        
        shell_fprintf(sh, SHELL_NORMAL, "Static IP configuration set:\n");
        shell_fprintf(sh, SHELL_NORMAL, "IP Address: %s\n", argv[2]);
        shell_fprintf(sh, SHELL_NORMAL, "Netmask: %s\n", argv[3]);
        shell_fprintf(sh, SHELL_NORMAL, "Gateway: %s\n", argv[4]);
        shell_fprintf(sh, SHELL_NORMAL, "Use 'net ipv4' to verify configuration\n");
    } else {
        shell_fprintf(sh, SHELL_ERROR, "Unknown subcommand: %s\n", argv[1]);
        shell_fprintf(sh, SHELL_ERROR, 
                     "Usage: wifi ip set <ip_addr> <netmask> <gateway>\n");
        return -EINVAL;
    }

    return 0;
}

/* Network testing commands */
static int cmd_wifi_ping(const struct shell *sh, size_t argc, char *argv[])
{
    if (argc < 2) {
        shell_fprintf(sh, SHELL_ERROR, "Usage: wifi ping <hostname/ip>\n");
        return -EINVAL;
    }

    shell_fprintf(sh, SHELL_NORMAL, "To ping %s, use the net ping command:\n", argv[1]);
    shell_fprintf(sh, SHELL_NORMAL, "  net ping %s\n", argv[1]);
    shell_fprintf(sh, SHELL_NORMAL, "This will test network connectivity using ICMP echo requests.\n");
    
    return 0;
}

/* Network troubleshooting command */
static int cmd_wifi_diagnose(const struct shell *sh, size_t argc, char *argv[])
{
    shell_fprintf(sh, SHELL_NORMAL, "WiFi Network Diagnostic Report\n");
    shell_fprintf(sh, SHELL_NORMAL, "===========================\n\n");
    
    /* 1. Check WiFi connection */
    shell_fprintf(sh, SHELL_NORMAL, "Checking WiFi connection...\n");
    const struct device *emw_dev = get_emw3080_device();
    if (!emw_dev) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 device found!\n");
        shell_fprintf(sh, SHELL_NORMAL, "Recommendation: Check hardware initialization\n");
        return -ENODEV;
    }
    
    struct net_if *iface = NULL;
    int i;
    
    /* Find network interface for the EMW3080 device */
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
        shell_fprintf(sh, SHELL_ERROR, "No network interface found for EMW3080!\n");
        shell_fprintf(sh, SHELL_NORMAL, "Recommendation: Check driver initialization\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "EMW3080 interface found: #%d\n", net_if_get_by_iface(iface));
    shell_fprintf(sh, SHELL_NORMAL, "Interface status: %s\n", 
                 net_if_is_up(iface) ? "UP" : "DOWN");
    
    if (!net_if_is_up(iface)) {
        shell_fprintf(sh, SHELL_WARNING, "Interface is DOWN! Try 'wifi power on' to enable it.\n");
    }
    
    /* 2. Check WiFi connection status */
    shell_fprintf(sh, SHELL_NORMAL, "\nChecking WiFi connection status...\n");
    struct wifi_iface_status status = {0};
    int err = emw3080_mgmt_get_status(emw_dev, &status);
    
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Failed to get WiFi status: %d\n", err);
    } else {
        if (status.ssid_len > 0) {
            char safe_ssid[33] = {0};
            strncpy(safe_ssid, status.ssid, status.ssid_len > 32 ? 32 : status.ssid_len);
            shell_fprintf(sh, SHELL_NORMAL, "Connected to WiFi network: %s\n", safe_ssid);
            shell_fprintf(sh, SHELL_NORMAL, "Signal strength: %d dBm\n", status.rssi);
            shell_fprintf(sh, SHELL_NORMAL, "Channel: %u\n", status.channel);
        } else {
            shell_fprintf(sh, SHELL_WARNING, "Not connected to any WiFi network!\n");
            shell_fprintf(sh, SHELL_NORMAL, "Recommendation: Use 'wifi connect <SSID> <PSK>' to connect\n");
        }
    }
    
    /* 3. Check IP configuration */
    shell_fprintf(sh, SHELL_NORMAL, "\nChecking IP configuration...\n");
    bool has_ipv4 = false;
    
    /* Use the net_if IPv4 API available in this version */
    struct in_addr *addr4 = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
    if (addr4) {
        char addr_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, addr4, addr_str, sizeof(addr_str));
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: %s\n", addr_str);
        has_ipv4 = true;
    }
    
    /* Get gateway address */
    struct in_addr gw = net_if_ipv4_get_gw(iface);
    if (gw.s_addr) {
        char gw_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &gw, gw_str, sizeof(gw_str));
        shell_fprintf(sh, SHELL_NORMAL, "Gateway: %s\n", gw_str);
    } else {
        shell_fprintf(sh, SHELL_WARNING, "No gateway configured!\n");
    }
    
    if (!has_ipv4) {
        shell_fprintf(sh, SHELL_WARNING, "No IPv4 address configured!\n");
        shell_fprintf(sh, SHELL_NORMAL, "Recommendations:\n");
        shell_fprintf(sh, SHELL_NORMAL, "1. Try 'wifi dhcp start' to request an IP address\n");
        shell_fprintf(sh, SHELL_NORMAL, "2. Or configure static IP with 'wifi ip set <ip> <mask> <gw>'\n");
        shell_fprintf(sh, SHELL_NORMAL, "3. Check if your router's DHCP server is working\n");
    }
    
    /* 4. Check DHCP status */
    shell_fprintf(sh, SHELL_NORMAL, "\nChecking DHCP status...\n");
    
    /* In newer Zephyr versions, DHCP API has changed */
    net_dhcpv4_start(iface);
    
    /* Now check if DHCP is enabled using a different method since the API has changed */
    if (net_if_ipv4_get_global_addr(iface, NET_ADDR_DHCP) != NULL) {
        /* DHCP was already running */
        shell_fprintf(sh, SHELL_NORMAL, "DHCP client is active\n");
        
        /* Check if an IP address has been assigned */
        if (has_ipv4) {
            shell_fprintf(sh, SHELL_NORMAL, "DHCP state: IP address acquired\n");
        } else {
            shell_fprintf(sh, SHELL_NORMAL, "DHCP state: Attempting to get IP address\n");
            shell_fprintf(sh, SHELL_NORMAL, "Recommendation: Wait a few seconds and check again\n");
        }
    } else {
        /* DHCP wasn't running, so we just started it - stop it again */
        net_dhcpv4_stop(iface);
        shell_fprintf(sh, SHELL_NORMAL, "DHCP client is not active\n");
    }
    
    /* Overall recommendations */
    shell_fprintf(sh, SHELL_NORMAL, "\nSummary & Recommendations:\n");
    shell_fprintf(sh, SHELL_NORMAL, "------------------------\n");
    
    if (!status.ssid_len) {
        shell_fprintf(sh, SHELL_NORMAL, "* Connect to WiFi first: wifi connect <SSID> <PSK>\n");
    } else if (!has_ipv4) {
        shell_fprintf(sh, SHELL_NORMAL, "* Start DHCP client: wifi dhcp start\n");
        shell_fprintf(sh, SHELL_NORMAL, "* Or configure static IP: wifi ip set <ip> <mask> <gw>\n");
    } else {
        shell_fprintf(sh, SHELL_NORMAL, "* Your network appears to be configured correctly\n");
        shell_fprintf(sh, SHELL_NORMAL, "* Try 'net ping 8.8.8.8' to test Internet connectivity\n");
    }
    
    return 0;
}

/* SPI register test command */
static int cmd_spi_test(const struct shell *sh, size_t argc, char **argv)
{
    shell_fprintf(sh, SHELL_NORMAL, "EMW3080 SPI Register Test\n");
    shell_fprintf(sh, SHELL_NORMAL, "========================\n");
    
    /* Get the SPI device directly */
    const struct device *spi_dev = device_get_binding("spi@40003800");
    if (!spi_dev) {
        shell_fprintf(sh, SHELL_ERROR, "ERROR: Cannot find SPI device spi@40003800\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Found SPI device: %s\n", spi_dev->name);
    shell_fprintf(sh, SHELL_NORMAL, "SPI device ready: %s\n", device_is_ready(spi_dev) ? "YES" : "NO");
    
    if (!device_is_ready(spi_dev)) {
        shell_fprintf(sh, SHELL_ERROR, "ERROR: SPI device not ready\n");
        return -ENODEV;
    }
    
    /* Include the SPI functions */
    extern int emw3080_spi_transceive(const struct device *spi_dev, 
                                     const void *tx_buf, size_t tx_len,
                                     void *rx_buf, size_t rx_len);
    
    /* Test 1: Basic SPI communication test */
    shell_fprintf(sh, SHELL_NORMAL, "\nTest 1: Basic SPI Ping Test\n");
    shell_fprintf(sh, SHELL_NORMAL, "----------------------------\n");
    
    uint8_t tx_test[4] = {0xAA, 0x55, 0xFF, 0x00};  /* Test pattern */
    uint8_t rx_test[4] = {0};
    
    int ret = emw3080_spi_transceive(spi_dev, tx_test, sizeof(tx_test), rx_test, sizeof(rx_test));
    
    shell_fprintf(sh, SHELL_NORMAL, "TX: %02X %02X %02X %02X\n", 
                  tx_test[0], tx_test[1], tx_test[2], tx_test[3]);
    shell_fprintf(sh, SHELL_NORMAL, "RX: %02X %02X %02X %02X\n", 
                  rx_test[0], rx_test[1], rx_test[2], rx_test[3]);
    shell_fprintf(sh, SHELL_NORMAL, "Result: %s (%d)\n", ret == 0 ? "SUCCESS" : "FAILED", ret);
    
    /* Test 2: EMW3080 Status Register Read */
    shell_fprintf(sh, SHELL_NORMAL, "\nTest 2: EMW3080 Status Register\n");
    shell_fprintf(sh, SHELL_NORMAL, "-------------------------------\n");
    
    uint8_t status_cmd = 0x04;  /* EMW3080_SPI_STATUS_CMD */
    uint8_t status_val = 0;
    
    ret = emw3080_spi_transceive(spi_dev, &status_cmd, 1, &status_val, 1);
    
    shell_fprintf(sh, SHELL_NORMAL, "Status CMD: 0x%02X\n", status_cmd);
    shell_fprintf(sh, SHELL_NORMAL, "Status VAL: 0x%02X\n", status_val);
    shell_fprintf(sh, SHELL_NORMAL, "Result: %s (%d)\n", ret == 0 ? "SUCCESS" : "FAILED", ret);
    
    if (ret == 0) {
        shell_fprintf(sh, SHELL_NORMAL, "Status Decode:\n");
        shell_fprintf(sh, SHELL_NORMAL, "  Ready: %s (bit 0)\n", 
                      (status_val & 0x01) ? "NOT READY" : "READY");
        shell_fprintf(sh, SHELL_NORMAL, "  Data Available: %s (bit 1)\n", 
                      (status_val & 0x02) ? "YES" : "NO");
        shell_fprintf(sh, SHELL_NORMAL, "  Busy: %s (bit 2)\n", 
                      (status_val & 0x04) ? "YES" : "NO");
    }
    
    /* Test 3: Multiple Status Reads */
    shell_fprintf(sh, SHELL_NORMAL, "\nTest 3: Multiple Status Reads\n");
    shell_fprintf(sh, SHELL_NORMAL, "-----------------------------\n");
    
    for (int i = 0; i < 5; i++) {
        ret = emw3080_spi_transceive(spi_dev, &status_cmd, 1, &status_val, 1);
        shell_fprintf(sh, SHELL_NORMAL, "Read %d: 0x%02X (%s)\n", 
                      i+1, status_val, ret == 0 ? "OK" : "FAIL");
        k_msleep(100);  /* Wait between reads */
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "\nSPI Test Complete\n");
    return 0;
}

/* MIPC test commands */
static int cmd_mipc_init(const struct shell *sh, size_t argc, char **argv)
{
    shell_fprintf(sh, SHELL_NORMAL, "Initializing MIPC protocol over SPI...\n");
    
    int ret = emw3080_mipc_spi_init();
    if (ret == 0) {
        shell_fprintf(sh, SHELL_NORMAL, "MIPC initialization successful\n");
    } else {
        shell_fprintf(sh, SHELL_ERROR, "MIPC initialization failed: %d\n", ret);
    }
    
    return ret;
}

static int cmd_mipc_echo(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_fprintf(sh, SHELL_ERROR, "Usage: wifi mipcecho <test_string>\n");
        return -EINVAL;
    }
    
    const char *test_string = argv[1];
    uint8_t response_buffer[256];
    uint16_t response_size = sizeof(response_buffer);
    
    shell_fprintf(sh, SHELL_NORMAL, "Testing MIPC echo with: '%s'\n", test_string);
    
    int ret = mipc_echo((uint8_t *)test_string, strlen(test_string),
                       response_buffer, &response_size, 5000);
    
    if (ret == MIPC_CODE_SUCCESS) {
        shell_fprintf(sh, SHELL_NORMAL, "MIPC echo successful, received %d bytes:\n", response_size);
        
        /* Print response as string if printable */
        bool printable = true;
        for (int i = 0; i < response_size; i++) {
            if (response_buffer[i] < 32 || response_buffer[i] > 126) {
                printable = false;
                break;
            }
        }
        
        if (printable && response_size > 0) {
            response_buffer[response_size] = '\0';
            shell_fprintf(sh, SHELL_NORMAL, "Response: '%s'\n", response_buffer);
        } else {
            shell_fprintf(sh, SHELL_NORMAL, "Response (hex): ");
            for (int i = 0; i < response_size; i++) {
                shell_fprintf(sh, SHELL_NORMAL, "%02x ", response_buffer[i]);
            }
            shell_fprintf(sh, SHELL_NORMAL, "\n");
        }
    } else {
        shell_fprintf(sh, SHELL_ERROR, "MIPC echo failed: %d\n", ret);
    }
    
    return ret == MIPC_CODE_SUCCESS ? 0 : -1;
}

static int cmd_mipc_version(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t response_buffer[256];
    uint16_t response_size = sizeof(response_buffer);
    
    shell_fprintf(sh, SHELL_NORMAL, "Getting system version via MIPC...\n");
    
    int ret = mipc_request(MIPC_API_SYS_VERSION_CMD, NULL, 0,
                          response_buffer, &response_size, 5000);
    
    if (ret == MIPC_CODE_SUCCESS) {
        shell_fprintf(sh, SHELL_NORMAL, "Version request successful, received %d bytes:\n", response_size);
        
        /* Print response as hex and try as string */
        shell_fprintf(sh, SHELL_NORMAL, "Response (hex): ");
        for (int i = 0; i < response_size; i++) {
            shell_fprintf(sh, SHELL_NORMAL, "%02x ", response_buffer[i]);
        }
        shell_fprintf(sh, SHELL_NORMAL, "\n");
        
        /* Try to interpret as string */
        if (response_size > 0) {
            shell_fprintf(sh, SHELL_NORMAL, "Response (string): ");
            for (int i = 0; i < response_size; i++) {
                char c = response_buffer[i];
                shell_fprintf(sh, SHELL_NORMAL, "%c", (c >= 32 && c <= 126) ? c : '.');
            }
            shell_fprintf(sh, SHELL_NORMAL, "\n");
        }
    } else {
        shell_fprintf(sh, SHELL_ERROR, "Version request failed: %d\n", ret);
    }
    
    return ret == MIPC_CODE_SUCCESS ? 0 : -1;
}

static int cmd_mipc_getmac(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t response_buffer[256];
    uint16_t response_size = sizeof(response_buffer);
    
    shell_fprintf(sh, SHELL_NORMAL, "Getting MAC address via MIPC...\n");
    
    int ret = mipc_request(MIPC_API_WIFI_GET_MAC_CMD, NULL, 0,
                          response_buffer, &response_size, 5000);
    
    if (ret == MIPC_CODE_SUCCESS) {
        shell_fprintf(sh, SHELL_NORMAL, "MAC request successful, received %d bytes:\n", response_size);
        
        /* Print response as hex */
        shell_fprintf(sh, SHELL_NORMAL, "Response (hex): ");
        for (int i = 0; i < response_size; i++) {
            shell_fprintf(sh, SHELL_NORMAL, "%02x ", response_buffer[i]);
        }
        shell_fprintf(sh, SHELL_NORMAL, "\n");
        
        /* If we received 6 bytes, interpret as MAC address */
        if (response_size >= 6) {
            shell_fprintf(sh, SHELL_NORMAL, "MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
                         response_buffer[0], response_buffer[1], response_buffer[2],
                         response_buffer[3], response_buffer[4], response_buffer[5]);
        }
    } else {
        shell_fprintf(sh, SHELL_ERROR, "MAC request failed: %d\n", ret);
    }
    
    return ret == MIPC_CODE_SUCCESS ? 0 : -1;
}

static int cmd_mipc_poll(const struct shell *sh, size_t argc, char **argv)
{
    shell_fprintf(sh, SHELL_NORMAL, "Polling for MIPC responses...\n");
    emw3080_mipc_spi_poll();
    shell_fprintf(sh, SHELL_NORMAL, "Poll completed\n");
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(wifi_cmds,
    SHELL_CMD(scan, NULL, "Scan for Wi-Fi networks", cmd_wifi_scan),
    SHELL_CMD(connect, NULL, "Connect: wifi connect <SSID> <PSK> [security_type]", cmd_wifi_connect),
    SHELL_CMD(disconnect, NULL, "Disconnect from Wi-Fi network", cmd_wifi_disconnect),
    SHELL_CMD(status, NULL, "Show Wi-Fi interface status", cmd_wifi_status),
    SHELL_CMD(power, NULL, "Power on/off Wi-Fi: wifi power <on|off>", cmd_wifi_power),
    SHELL_CMD(dhcp, NULL, "Start/stop DHCP: wifi dhcp <start|stop>", cmd_wifi_dhcp),
    SHELL_CMD(ip, NULL, "Configure static IP: wifi ip set <ip> <netmask> <gw>", cmd_wifi_ip),
    SHELL_CMD(ping, NULL, "Network test: wifi ping <ip/hostname>", cmd_wifi_ping),
    SHELL_CMD(diagnose, NULL, "Run network diagnostics", cmd_wifi_diagnose),
    SHELL_CMD(spitest, NULL, "Test SPI communication with EMW3080", cmd_spi_test),
    SHELL_CMD(mipc_init, NULL, "Initialize MIPC protocol", cmd_mipc_init),
    SHELL_CMD(mipcecho, NULL, "Test MIPC echo: wifi mipcecho <string>", cmd_mipc_echo),
    SHELL_CMD(mipcver, NULL, "Get system version via MIPC", cmd_mipc_version),
    SHELL_CMD(mipcmac, NULL, "Get MAC address via MIPC", cmd_mipc_getmac),
    SHELL_CMD(mipcpoll, NULL, "Poll for MIPC responses", cmd_mipc_poll),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(wifi, &wifi_cmds, "Wi-Fi commands", NULL);
