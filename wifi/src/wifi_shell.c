#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/offloaded_netdev.h>
#include <string.h>

/* Helper function to get Wi-Fi interface - completely rewritten for maximum safety */
static struct net_if *get_wifi_iface(void)
{
    /* First, always try our EMW3080 name-based lookup for testing */
    struct net_if *iface = NULL;
    int i = 0;
    
    shell_print(NULL, "DEBUG: Starting WiFi interface search by name...");
    
    /* SAFETY FIRST: Look for EMW3080 in device name - most reliable approach */
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
            shell_print(NULL, "DEBUG: Found EMW3080 interface by name: %s", dev->name);
            return tmp;  /* Return immediately when we find a match */
        }
    }
    
    /* FALLBACK: If EMW3080 not found by name, look for any network interface */
    shell_print(NULL, "DEBUG: EMW3080 not found by name, checking all interfaces...");
    
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp = net_if_get_by_index(i);
        if (!tmp) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(tmp);
        if (!dev) {
            continue;
        }
        
        /* Log basic info about this interface */
        shell_print(NULL, "DEBUG: Found interface %d with device: %s", 
                  i, dev->name ? dev->name : "unknown");
        
        /* Save first interface as fallback option */
        if (!iface) {
            iface = tmp;
            shell_print(NULL, "DEBUG: Saving as fallback option");
        }
    }
    
    /* Last resort: Try the default interface */
    if (!iface) {
        iface = net_if_get_default();
        if (iface) {
            const struct device *dev = net_if_get_device(iface);
            shell_print(NULL, "DEBUG: Using default interface: %s", 
                      dev ? (dev->name ? dev->name : "unnamed") : "unknown device");
        } else {
            shell_print(NULL, "DEBUG: No default interface available");
        }
    } else {
        const struct device *dev = net_if_get_device(iface);
        shell_print(NULL, "DEBUG: Using interface found during scan: %s", 
                   dev ? (dev->name ? dev->name : "unnamed") : "unknown device");
    }
    
    return iface;  /* Return whatever we found, or NULL if nothing */
}

/* Wi-Fi scan command - completely rewritten for safety */
static int cmd_wifi_scan(const struct shell *sh, size_t argc, char *argv[])
{
    /* Print diagnostic info about all interfaces */
    struct net_if *all_iface;
    int i = 0;
    
    shell_fprintf(sh, SHELL_NORMAL, "Checking interfaces by name:\n");
    
    /* Safely iterate through all interfaces - AVOID any L2 or WiFi capability checks */
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        all_iface = net_if_get_by_index(i);
        if (!all_iface) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(all_iface);
        
        /* Only show basic device info - AVOID any L2 or WiFi capability checks */
        shell_fprintf(sh, SHELL_NORMAL, "IF[%d]: %s\n", i, 
                    (dev && dev->name) ? dev->name : "unknown");
        
        /* Identify EMW3080 interfaces by name */
        bool is_emw3080 = false;
        if (dev && dev->name && strstr(dev->name, "EMW3080") != NULL) {
            is_emw3080 = true;
            shell_fprintf(sh, SHELL_NORMAL, "          EMW3080 interface detected by name\n");
        }
                  
        /* Only access API if device exists */
        if (dev && dev->api) {
            /* Log the API pointer for debugging */
            shell_fprintf(sh, SHELL_NORMAL, "          API pointer: %p\n", dev->api);
        }
    }
    
    /* Try to get the WiFi interface using our safer function */
    shell_fprintf(sh, SHELL_NORMAL, "\nDEBUG: Looking for EMW3080 WiFi interface by name...\n");
    struct net_if *iface = get_wifi_iface();
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
        return -ENODEV;
    }

    /* Use the interface to start a scan */
    shell_fprintf(sh, SHELL_NORMAL, "Starting Wi-Fi scan on interface %p...\n", iface);
    
    /* Log device info for the scan */
    const struct device *scan_dev = net_if_get_device(iface);
    if (scan_dev) {
        shell_fprintf(sh, SHELL_NORMAL, "Using device: %s\n", 
                     scan_dev->name ? scan_dev->name : "unnamed");
    }
    
    /* Use a proper scan params structure instead of NULL to avoid memory issues */
    struct wifi_scan_params scan_params = {0};
    
    /* Request the scan safely */
    shell_fprintf(sh, SHELL_NORMAL, "Sending scan request with properly initialized params...\n");
    
    /* CRITICAL SAFETY: Use NET_REQUEST_WIFI_SCAN directly with fully initialized parameters */
    shell_fprintf(sh, SHELL_NORMAL, "Executing NET_REQUEST_WIFI_SCAN network management request\n");
    int err = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, &scan_params, sizeof(scan_params));
    
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Failed to start scan: %d\n", err);
        return -EIO;
    }

    shell_fprintf(sh, SHELL_NORMAL, "Scan requested. Results will be reported via events.\n");
    return 0;
}

/* Wi-Fi connect command */
static int cmd_wifi_connect(const struct shell *sh, size_t argc, char *argv[])
{
    struct net_if *iface = get_wifi_iface();
    
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
        return -ENODEV;
    }
    
    if (argc < 3) {
        shell_fprintf(sh, SHELL_ERROR,
                      "Usage: wifi connect <SSID> <PSK>\n");
        return -EINVAL;
    }

    struct wifi_connect_req_params params = { 0 };
    
    params.ssid = argv[1];
    params.ssid_length = strlen(argv[1]);
    
    params.psk = argv[2];
    params.psk_length = strlen(argv[2]);
    
    params.channel = WIFI_CHANNEL_ANY;
    params.security = WIFI_SECURITY_TYPE_PSK;
    
    shell_fprintf(sh, SHELL_NORMAL, "Connecting to SSID: %s...\n", argv[1]);
    
    if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params))) {
        shell_fprintf(sh, SHELL_ERROR, "Connection request failed\n");
        return -EIO;
    }

    return 0;
}

/* Wi-Fi disconnect command */
static int cmd_wifi_disconnect(const struct shell *sh, size_t argc, char *argv[])
{
    struct net_if *iface = get_wifi_iface();
    
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
        return -ENODEV;
    }

    shell_fprintf(sh, SHELL_NORMAL, "Disconnecting from Wi-Fi network...\n");
    
    if (net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0)) {
        shell_fprintf(sh, SHELL_ERROR, "Disconnect request failed\n");
        return -EIO;
    }

    return 0;
}

/* Wi-Fi status command - improved for safety */
static int cmd_wifi_status(const struct shell *sh, size_t argc, char *argv[])
{
    shell_fprintf(sh, SHELL_NORMAL, "Looking for Wi-Fi interface...\n");
    
    /* Use our safe get_wifi_iface function to find the interface */
    struct net_if *iface = get_wifi_iface();
    
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Wi-Fi Interface: %p\n", iface);
    
    /* Basic interface status - is it up? */
    shell_fprintf(sh, SHELL_NORMAL, "Status: %s\n", 
                  net_if_is_up(iface) ? "UP" : "DOWN");
    
    /* Get device info safely */
    const struct device *dev = net_if_get_device(iface);
    shell_fprintf(sh, SHELL_NORMAL, "Device: %s\n", 
                 (dev && dev->name) ? dev->name : "unknown");
    
    /* Simple check based on name only - AVOID L2 checks completely */
    shell_fprintf(sh, SHELL_NORMAL, "EMW3080 Device: ");
    if (dev && dev->name) {
        shell_fprintf(sh, SHELL_NORMAL, "%s\n", 
                    (strstr(dev->name, "EMW3080") != NULL) ? "Yes" : "No");
    } else {
        shell_fprintf(sh, SHELL_NORMAL, "Unknown (no device name)\n");
    }
                 
    /* Let's try to retrieve WiFi status using mgmt interface with extra safety */
    shell_fprintf(sh, SHELL_NORMAL, "Attempting to retrieve WiFi status from driver...\n");
    
    /* Use a local variable with zeroed memory */
    struct wifi_iface_status status;
    memset(&status, 0, sizeof(status));
    
    /* CRITICAL SAFETY: Use try/catch style error handling for any net_mgmt calls */
    int err = -1;
    
    /* Wrap the call in a safer approach */
    if (iface) {
        /* Use NET_REQUEST_WIFI_IFACE_STATUS with properly initialized params */
        err = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
        
        if (err == 0) {
            shell_fprintf(sh, SHELL_NORMAL, "WiFi State: %d\n", status.state);
            /* Validate SSID before printing */
            if (status.ssid_len > 0 && status.ssid_len <= sizeof(status.ssid)) {
                shell_fprintf(sh, SHELL_NORMAL, "SSID: %.*s\n", status.ssid_len, status.ssid);
            } else {
                shell_fprintf(sh, SHELL_NORMAL, "SSID: <invalid>\n");
            }
            shell_fprintf(sh, SHELL_NORMAL, "RSSI: %d\n", status.rssi);
            shell_fprintf(sh, SHELL_NORMAL, "Channel: %d\n", status.channel);
            shell_fprintf(sh, SHELL_NORMAL, "Security: %d\n", status.security);
        } else {
            shell_fprintf(sh, SHELL_WARNING, "Could not retrieve WiFi status: %d\n", err);
            shell_fprintf(sh, SHELL_WARNING, "This is normal if WiFi driver doesn't implement the status API\n");
        }
    } else {
        shell_fprintf(sh, SHELL_ERROR, "Invalid interface for WiFi status check\n");
    }
    
    /* Check for IPv4 configuration - use a much safer approach */
    shell_fprintf(sh, SHELL_NORMAL, "Checking for IPv4 configuration...\n");
    
    /* Basic validation only - avoid accessing config details that might be invalid */
    if (!iface) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 configuration not available (no interface)\n");
        return 0;
    }
    
    /* Don't try to access IPv4 configuration directly as it might be missing */
    shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: Use 'net ipv4' command for details\n");

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
