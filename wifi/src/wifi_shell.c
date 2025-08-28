#include <zephyr/shell/shell.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/offloaded_netdev.h>
#include <string.h>

/* Helper function to get Wi-Fi interface */
static struct net_if *get_wifi_iface(void)
{
    /* First, always try our EMW3080 name-based lookup for testing */
    struct net_if *iface = NULL;
    int i = 0;
    
    shell_print(NULL, "DEBUG: Starting WiFi interface search, looking at all interfaces...");
    
    /* Step 1: First try to find the EMW3080 interface by name - safest option */
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp = net_if_get_by_index(i);
        if (!tmp) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(tmp);
        if (!dev) {
            shell_print(NULL, "DEBUG: Interface %d has no device", i);
            continue;
        }
        
        /* First check if the device name is available and contains "EMW3080" */
        if (dev->name && strstr(dev->name, "EMW3080") != NULL) {
            shell_print(NULL, "DEBUG: Found EMW3080 interface by name: %s", dev->name);
            return tmp;
        }
        
        /* Log information about each interface */
        shell_print(NULL, "DEBUG: Checking interface %d - device name: %s", 
                  i, dev->name ? dev->name : "unknown");
        
        /* Only try to access the API structure if the device is valid */
        if (dev->api) {
            shell_print(NULL, "DEBUG:   - API pointer: %p", dev->api);
            
            /* Try to check if the interface is WiFi without accessing potentially invalid memory */
            bool is_wifi = net_if_is_wifi(tmp);
            bool is_offloaded_wifi = net_off_is_wifi_offloaded(tmp);
            
            shell_print(NULL, "DEBUG:   - net_if_is_wifi() returns: %d", is_wifi);
            shell_print(NULL, "DEBUG:   - net_off_is_wifi_offloaded() returns: %d", is_offloaded_wifi);
            
            /* If this interface claims to be WiFi, save it as a backup */
            if (is_wifi || is_offloaded_wifi) {
                shell_print(NULL, "DEBUG:   - This interface claims to be WiFi, saving as backup option");
                if (!iface) {
                    iface = tmp;
                }
            }
        } else {
            shell_print(NULL, "DEBUG:   - No API structure");
        }
    }
    
    /* Step 2: If we found a WiFi interface in the scan, use it */
    if (iface) {
        const struct device *dev = net_if_get_device(iface);
        shell_print(NULL, "DEBUG: Using WiFi interface found during scan: %s", 
                   dev ? (dev->name ? dev->name : "unnamed") : "unknown device");
        return iface;
    }
    
    /* Step 3: Fall back to standard lookup API */
    shell_print(NULL, "DEBUG: No WiFi interface found by name or capabilities, trying net_if_get_first_wifi()...");
    
    iface = net_if_get_first_wifi();
    if (iface) {
        const struct device *dev = net_if_get_device(iface);
        shell_print(NULL, "DEBUG: Found WiFi interface via net_if_get_first_wifi(): %s", 
                   dev ? (dev->name ? dev->name : "unnamed") : "unknown device");
        return iface;
    }
    
    /* Step 4: Last resort - try the default interface */
    shell_print(NULL, "DEBUG: No WiFi interface found, using default interface as last resort");
    
    iface = net_if_get_default();
    if (iface) {
        const struct device *dev = net_if_get_device(iface);
        shell_print(NULL, "DEBUG: Using default interface: %s", 
                   dev ? (dev->name ? dev->name : "unnamed") : "unknown device");
        return iface;
    }
    
    shell_print(NULL, "DEBUG: No network interfaces found in the system");
    return NULL;
}

/* Wi-Fi scan command */
static int cmd_wifi_scan(const struct shell *sh, size_t argc, char *argv[])
{
    /* Print diagnostic info about all interfaces */
    struct net_if *all_iface;
    int i = 0;
    
    shell_fprintf(sh, SHELL_NORMAL, "Checking interfaces for WiFi capability:\n");
    
    /* Safely iterate through all interfaces */
    for (i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        all_iface = net_if_get_by_index(i);
        if (!all_iface) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(all_iface);
        
        /* Basic interface info without accessing API structures directly */
        shell_fprintf(sh, SHELL_NORMAL, "IF[%d]: %s - WiFi=%d, Offloaded WiFi=%d\n", 
                  i, 
                  (dev && dev->name) ? dev->name : "unknown",
                  net_if_is_wifi(all_iface),
                  net_off_is_wifi_offloaded(all_iface));
                  
        /* Only access API if device exists */
        if (dev && dev->api) {
            /* Log the API pointer for debugging */
            shell_fprintf(sh, SHELL_NORMAL, "          API pointer: %p\n", dev->api);
            
            /* Check if it's an offloaded device (using safer approach) */
            if (net_off_is_wifi_offloaded(all_iface)) {
                shell_fprintf(sh, SHELL_NORMAL, "          Interface is a WiFi offloaded interface\n");
            }
        }
    }
    
    /* Add debug info on how the network stack detects WiFi interfaces */
    shell_fprintf(sh, SHELL_NORMAL, "\nDEBUG: WiFi Detection Implementation Check\n");
    shell_fprintf(sh, SHELL_NORMAL, "---------------------------------------\n");
    shell_fprintf(sh, SHELL_NORMAL, "L2_OFFLOADED_NET_IF_TYPE_WIFI value: %d\n", L2_OFFLOADED_NET_IF_TYPE_WIFI);
    
    /* Try to get the WiFi interface using our safer function */
    shell_fprintf(sh, SHELL_NORMAL, "\nDEBUG: Now calling get_wifi_iface()...\n");
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
    
    /* Request the scan safely */
    int err = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
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

/* Wi-Fi status command */
static int cmd_wifi_status(const struct shell *sh, size_t argc, char *argv[])
{
    shell_fprintf(sh, SHELL_NORMAL, "Looking for Wi-Fi interface...\n");
    
    /* Safe retrieval of WiFi interface with detailed logging */
    struct net_if *iface = NULL;
    
    /* First, try to find an EMW3080 interface by name - safest option */
    for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        struct net_if *tmp = net_if_get_by_index(i);
        if (!tmp) {
            continue;
        }
        
        const struct device *dev = net_if_get_device(tmp);
        if (dev && dev->name && strstr(dev->name, "EMW3080") != NULL) {
            iface = tmp;
            shell_fprintf(sh, SHELL_NORMAL, "Found EMW3080 interface by name: %s\n", dev->name);
            break;
        }
    }
    
    /* If no EMW3080 interface found, try the standard WiFi API */
    if (!iface) {
        iface = net_if_get_default();
        if (iface) {
            shell_fprintf(sh, SHELL_NORMAL, "Using default interface\n");
        } else {
            shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
            return -ENODEV;
        }
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Wi-Fi Interface: %p\n", iface);
    
    /* Safe check before accessing iface */
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "Interface pointer is null\n");
        return -ENODEV;
    }
    
    shell_fprintf(sh, SHELL_NORMAL, "Status: %s\n", 
                  net_if_is_up(iface) ? "UP" : "DOWN");
    
    /* Get device info safely */
    const struct device *dev = net_if_get_device(iface);
    shell_fprintf(sh, SHELL_NORMAL, "Device: %s\n", dev ? dev->name : "unknown");
    
    /* Basic WiFi information without accessing driver structures */
    shell_fprintf(sh, SHELL_NORMAL, "WiFi capability: %s\n", 
                 net_if_is_wifi(iface) ? "Yes" : "No");
    shell_fprintf(sh, SHELL_NORMAL, "Offloaded WiFi: %s\n", 
                 net_off_is_wifi_offloaded(iface) ? "Yes" : "No");
                 
    /* Let's try to retrieve WiFi status using mgmt interface with extra safety */
    shell_fprintf(sh, SHELL_NORMAL, "Attempting to retrieve WiFi status from driver...\n");
    
    /* Use a local variable with zeroed memory */
    struct wifi_iface_status status;
    memset(&status, 0, sizeof(status));
    
    /* Explicitly validate iface before using with net_mgmt */
    if (iface) {
        int err = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
        
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
    
    /* Only try to display IPv4 info if we're confident it's available */
    shell_fprintf(sh, SHELL_NORMAL, "Checking for IPv4 configuration...\n");
    
    /* Ultra-defensive IPv4 config check */
    if (!iface) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 configuration not available (no interface)\n");
        return 0;
    }
    
    /* Check config structure before accessing ipv4 field */
    if (!iface->config.ip.ipv4) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 configuration not available\n");
        return 0;
    }
    
    struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
    
    /* Note: The compiler warns that this check is always true because
     * unicast is an array, not a pointer. Let's skip this check.
     */
    
    /* Instead, check if the first unicast address is valid */
    char addr_str[NET_IPV4_ADDR_LEN];
    
    if (net_ipv4_is_addr_unspecified(&ipv4->unicast[0].ipv4.address.in_addr)) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: Not assigned\n");
    } else {
        /* Use safe string conversion with proper length checks */
        if (net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr, 
                         addr_str, sizeof(addr_str))) {
            shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: %s\n", addr_str);
        } else {
            shell_fprintf(sh, SHELL_ERROR, "Could not convert IPv4 address to string\n");
        }
    }

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
