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
    struct net_if *iface;
    int i = 0;
    
    shell_print(NULL, "DEBUG: Starting WiFi interface search, looking at all interfaces...");
    
    while ((iface = net_if_get_by_index(i)) != NULL) {
        /* Check if this interface has our driver */
        const struct device *dev = net_if_get_device(iface);
        const char *dev_name = dev ? dev->name : "unknown";
        
        shell_print(NULL, "DEBUG: Checking interface %d - device name: %s", i, dev_name);
        
        if (dev != NULL && dev->api != NULL) {
            shell_print(NULL, "DEBUG:   - API pointer: %p", dev->api);
            
            /* Try to check if it's an offloaded device */
            const struct offloaded_if_api *off_api = (const struct offloaded_if_api *)dev->api;
            
            if (off_api && off_api->get_type) {
                shell_print(NULL, "DEBUG:   - Has get_type function: %p", off_api->get_type);
                enum offloaded_net_if_types type = off_api->get_type();
                shell_print(NULL, "DEBUG:   - get_type() returns: %d (WiFi=%d)", 
                          type, (type == L2_OFFLOADED_NET_IF_TYPE_WIFI) ? 1 : 0);
            } else {
                shell_print(NULL, "DEBUG:   - No get_type function found");
            }
            
            /* Check net_if_is_wifi() */
            shell_print(NULL, "DEBUG:   - net_if_is_wifi() returns: %d", net_if_is_wifi(iface));
            shell_print(NULL, "DEBUG:   - net_off_is_wifi_offloaded() returns: %d", net_off_is_wifi_offloaded(iface));
        }
        
        if (dev != NULL && strstr(dev->name, "EMW3080") != NULL) {
            shell_print(NULL, "Found EMW3080 interface by name: %s", dev->name);
            return iface;
        }
        i++;
    }
    
    shell_print(NULL, "DEBUG: No EMW3080 interface found by name, trying net_if_get_first_wifi()...");
    
    /* Fall back to standard lookup for production */
    iface = net_if_get_first_wifi();
    if (iface != NULL) {
        const struct device *dev = net_if_get_device(iface);
        shell_print(NULL, "Found WiFi interface: %s", dev ? dev->name : "unknown");
        return iface;
    }
    
    shell_print(NULL, "DEBUG: No WiFi interface found via net_if_get_first_wifi(), checking offloaded WiFi...");
    
    /* Fallback to check if interface is a WiFi offloaded interface */
    i = 0;
    while ((iface = net_if_get_by_index(i)) != NULL) {
        const struct device *dev = net_if_get_device(iface);
        shell_print(NULL, "DEBUG: Checking if interface %d (%s) is offloaded WiFi", 
                  i, dev ? dev->name : "unknown");
        
        if (net_off_is_wifi_offloaded(iface)) {
            shell_print(NULL, "Found offloaded WiFi interface: %s", dev ? dev->name : "unknown");
            return iface;
        }
        i++;
    }
    
    shell_print(NULL, "DEBUG: No WiFi interface found by any method");
    return NULL;
}

/* Wi-Fi scan command */
static int cmd_wifi_scan(const struct shell *sh, size_t argc, char *argv[])
{
    /* Print diagnostic info about all interfaces */
    struct net_if *all_iface;
    int i = 0;
    
    shell_fprintf(sh, SHELL_NORMAL, "Checking interfaces for WiFi capability:\n");
    while ((all_iface = net_if_get_by_index(i)) != NULL) {
        const struct device *dev = net_if_get_device(all_iface);
        const struct offloaded_if_api *api = NULL;
        
        if (dev && dev->api) {
            api = (const struct offloaded_if_api *)dev->api;
            shell_fprintf(sh, SHELL_NORMAL, "IF[%d]: %s - WiFi=%d, Offloaded WiFi=%d, Has API=%d, Has get_type=%d\n", 
                      i, 
                      dev ? dev->name : "unknown",
                      net_if_is_wifi(all_iface),
                      net_off_is_wifi_offloaded(all_iface),
                      (api != NULL) ? 1 : 0,
                      (api && api->get_type) ? 1 : 0);
                      
            if (api && api->get_type) {
                enum offloaded_net_if_types type = api->get_type();
                shell_fprintf(sh, SHELL_NORMAL, "          get_type() reports: %d (WiFi=%d)\n", 
                          (int)type, 
                          (type == L2_OFFLOADED_NET_IF_TYPE_WIFI) ? 1 : 0);
            }
            
            /* Extra debug info to see memory layout */
            shell_fprintf(sh, SHELL_NORMAL, "          API memory at %p, first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                      dev->api,
                      ((unsigned char*)dev->api)[0], ((unsigned char*)dev->api)[1],
                      ((unsigned char*)dev->api)[2], ((unsigned char*)dev->api)[3],
                      ((unsigned char*)dev->api)[4], ((unsigned char*)dev->api)[5],
                      ((unsigned char*)dev->api)[6], ((unsigned char*)dev->api)[7]);
        } else {
            shell_fprintf(sh, SHELL_NORMAL, "IF[%d]: %s - WiFi=%d, Offloaded WiFi=%d (no API)\n", 
                      i, 
                      dev ? dev->name : "unknown",
                      net_if_is_wifi(all_iface),
                      net_off_is_wifi_offloaded(all_iface));
        }
        i++;
    }
    
    /* Add debug info on how the network stack detects WiFi interfaces */
    shell_fprintf(sh, SHELL_NORMAL, "\nDEBUG: WiFi Detection Implementation Check\n");
    shell_fprintf(sh, SHELL_NORMAL, "---------------------------------------\n");
    shell_fprintf(sh, SHELL_NORMAL, "L2_OFFLOADED_NET_IF_TYPE_WIFI value: %d\n", L2_OFFLOADED_NET_IF_TYPE_WIFI);
    
    /* Try to get the WiFi interface */
    shell_fprintf(sh, SHELL_NORMAL, "\nDEBUG: Now calling get_wifi_iface()...\n");
    struct net_if *iface = get_wifi_iface();
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
        return -ENODEV;
    }

    shell_fprintf(sh, SHELL_NORMAL, "Starting Wi-Fi scan...\n");
    
    if (net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0)) {
        shell_fprintf(sh, SHELL_ERROR, "Failed to start scan\n");
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
    struct net_if *iface = get_wifi_iface();
    
    if (!iface) {
        shell_fprintf(sh, SHELL_ERROR, "No Wi-Fi interface found\n");
        return -ENODEV;
    }

    shell_fprintf(sh, SHELL_NORMAL, "Wi-Fi Interface: %p\n", iface);
    shell_fprintf(sh, SHELL_NORMAL, "Status: %s\n", 
                  net_if_is_up(iface) ? "UP" : "DOWN");
    
    /* Let's also try to retrieve WiFi status using mgmt interface */
    shell_fprintf(sh, SHELL_NORMAL, "Attempting to retrieve WiFi status from driver...\n");
    
    struct wifi_iface_status status = {0};
    int err = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
    if (!err) {
        shell_fprintf(sh, SHELL_NORMAL, "WiFi State: %d\n", status.state);
        shell_fprintf(sh, SHELL_NORMAL, "SSID: %s\n", status.ssid);
        shell_fprintf(sh, SHELL_NORMAL, "RSSI: %d\n", status.rssi);
        shell_fprintf(sh, SHELL_NORMAL, "Channel: %d\n", status.channel);
        shell_fprintf(sh, SHELL_NORMAL, "Security: %d\n", status.security);
    } else {
        shell_fprintf(sh, SHELL_ERROR, "Could not retrieve WiFi status: %d\n", err);
    }
    
    /* Display IPv4 address if available */
    shell_fprintf(sh, SHELL_NORMAL, "Checking for IPv4 configuration...\n");
    
    /* Defensive check to avoid null pointer dereference */
    if (!iface || !iface->config.ip.ipv4) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 configuration not available\n");
        return 0;
    }
    
    struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
    if (!ipv4) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 not configured\n");
        return 0;
    }
    
    /* Verify that unicast address is initialized */
    if (!ipv4->unicast) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 unicast addresses not initialized\n");
        return 0;
    }
    
    char addr_str[NET_IPV4_ADDR_LEN];
    
    if (net_ipv4_is_addr_unspecified(&ipv4->unicast[0].ipv4.address.in_addr)) {
        shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: Not assigned\n");
    } else {
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
