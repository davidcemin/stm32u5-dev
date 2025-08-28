#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/offloaded_netdev.h>
#include <string.h>

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

/* Wi-Fi scan command - completely rewritten for ULTRA safety */
static int cmd_wifi_scan(const struct shell *sh, size_t argc, char *argv[])
{
    /* ULTRA SAFE APPROACH - No assumptions, no complex structures */
    shell_fprintf(sh, SHELL_NORMAL, "Starting Wi-Fi scan in ultra-safe mode...\n");
    
    /* Safely list all interfaces first */
    struct net_if *all_iface;
    int i = 0;
    bool found_emw = false;
    struct net_if *emw_iface = NULL;
    
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
        if (dev && dev->name && strstr(dev->name, "EMW3080") != NULL) {
            shell_fprintf(sh, SHELL_NORMAL, "          EMW3080 interface detected by name\n");
            found_emw = true;
            emw_iface = all_iface;
        }
    }
    
    if (!found_emw) {
        shell_fprintf(sh, SHELL_ERROR, "No EMW3080 interface found by name\n");
        shell_fprintf(sh, SHELL_ERROR, "Cannot continue with scan operation\n");
        return -ENODEV;
    }
    
    if (!emw_iface) {
        shell_fprintf(sh, SHELL_ERROR, "EMW3080 interface found but is NULL\n");
        shell_fprintf(sh, SHELL_ERROR, "Cannot continue with scan operation\n");
        return -ENODEV;
    }

    /* Use the interface to start a scan */
    shell_fprintf(sh, SHELL_NORMAL, "Starting Wi-Fi scan on EMW3080 interface...\n");
    
    /* Log device info for the scan */
    const struct device *scan_dev = net_if_get_device(emw_iface);
    if (scan_dev) {
        shell_fprintf(sh, SHELL_NORMAL, "Using device: %s\n", 
                     scan_dev->name ? scan_dev->name : "unnamed");
    }
    
    /* Use a properly zeroed scan params structure to avoid memory issues */
    struct wifi_scan_params scan_params = {0};
    
    /* Request the scan safely */
    shell_fprintf(sh, SHELL_NORMAL, "Sending scan request with properly initialized params...\n");
    
    /* CRITICAL SAFETY: Use NET_REQUEST_WIFI_SCAN with fully initialized parameters */
    shell_fprintf(sh, SHELL_NORMAL, "Executing NET_REQUEST_WIFI_SCAN network management request\n");
    int err = net_mgmt(NET_REQUEST_WIFI_SCAN, emw_iface, &scan_params, sizeof(scan_params));
    
    if (err) {
        shell_fprintf(sh, SHELL_ERROR, "Failed to start scan: %d\n", err);
        return -EIO;
    }

    shell_fprintf(sh, SHELL_NORMAL, "Scan requested. Results will be reported via events.\n");
    shell_fprintf(sh, SHELL_NORMAL, "To see scan results, check for Wi-Fi scan result events.\n");
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

/* Wi-Fi status command - completely rewritten for maximum safety */
static int cmd_wifi_status(const struct shell *sh, size_t argc, char *argv[])
{
    /* ULTRA SAFE APPROACH - No assumptions, no complex structures */
    shell_fprintf(sh, SHELL_NORMAL, "Looking for Wi-Fi interface...\n");
    
    /* EMERGENCY FIX: DON'T ACCESS WIFI INTERFACE AT ALL */
    /* Instead of using get_wifi_iface(), just print basic info */
    shell_fprintf(sh, SHELL_NORMAL, "Wi-Fi Interface Status Report\n");
    shell_fprintf(sh, SHELL_NORMAL, "==========================\n");
    
    /* Show basic network interfaces info without any potentially dangerous access */
    shell_fprintf(sh, SHELL_NORMAL, "Network interfaces:\n");
    
    int i;
    bool found_emw = false;
    struct net_if *emw_iface = NULL;
    
    /* Iterate over all interfaces but don't try to check their capabilities */
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp = net_if_get_by_index(i);
        if (!tmp) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(tmp);
        if (!dev) {
            shell_fprintf(sh, SHELL_NORMAL, "Interface %d: <no device>\n", i);
            continue;
        }
        
        /* Only access device name if it exists */
        const char *name = (dev && dev->name) ? dev->name : "<unnamed>";
        shell_fprintf(sh, SHELL_NORMAL, "Interface %d: %s\n", i, name);
        
        /* Identify EMW3080 interfaces by name only */
        if (dev && dev->name && strstr(dev->name, "EMW3080") != NULL) {
            shell_fprintf(sh, SHELL_NORMAL, "  * EMW3080 interface detected\n");
            shell_fprintf(sh, SHELL_NORMAL, "  * Status: %s\n", 
                        net_if_is_up(tmp) ? "UP" : "DOWN");
            found_emw = true;
            emw_iface = tmp;
        }
    }
    
    if (!found_emw) {
        shell_fprintf(sh, SHELL_WARNING, "No EMW3080 interface found by name\n");
        shell_fprintf(sh, SHELL_WARNING, "To diagnose connectivity issues, try:\n");
        shell_fprintf(sh, SHELL_WARNING, "1. Check if the EMW3080 module is properly initialized\n");
        shell_fprintf(sh, SHELL_WARNING, "2. Check network interface status with 'net iface'\n");
        shell_fprintf(sh, SHELL_WARNING, "3. Check device list with 'device list'\n");
        return 0;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "\nEMW3080 connection status\n");
    shell_fprintf(sh, SHELL_NORMAL, "=======================\n");
    
    /* AVOID CHECKING IPv4 CONFIGURATION DIRECTLY */
    /* Just mention how to check it safely */
    shell_fprintf(sh, SHELL_NORMAL, "IP configuration: Use 'net ipv4' for details\n");
    
    /* SAFETY: Skip WiFi management status API entirely for now */
    /* Don't call net_mgmt for WIFI_IFACE_STATUS which might cause the bus fault */
    shell_fprintf(sh, SHELL_NORMAL, "WiFi connection details: Not available in safe mode\n");
    
    shell_fprintf(sh, SHELL_NORMAL, "\nSafety mode is active to prevent hardware faults.\n");
    shell_fprintf(sh, SHELL_NORMAL, "For network interface details, try 'net iface' instead.\n");
    
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
