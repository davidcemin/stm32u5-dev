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
    /* First try using the official WiFi interface lookup */
    struct net_if *iface = net_if_get_first_wifi();
    if (iface != NULL) {
        const struct device *dev = net_if_get_device(iface);
        shell_print(NULL, "Found WiFi interface: %s", dev ? dev->name : "unknown");
        return iface;
    }
    
    /* Fallback to check if interface is a WiFi offloaded interface */
    int i = 0;
    while ((iface = net_if_get_by_index(i)) != NULL) {
        if (net_off_is_wifi_offloaded(iface)) {
            const struct device *dev = net_if_get_device(iface);
            shell_print(NULL, "Found offloaded WiFi interface: %s", dev ? dev->name : "unknown");
            return iface;
        }
        i++;
    }
    
    /* Last fallback to manual device name search */
    i = 0;
    while ((iface = net_if_get_by_index(i)) != NULL) {
        /* Check if this interface has our driver */
        const struct device *dev = net_if_get_device(iface);
        if (dev != NULL && strstr(dev->name, "EMW3080") != NULL) {
            shell_print(NULL, "Found EMW3080 interface by name: %s", dev->name);
            return iface;
        }
        i++;
    }

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
        shell_fprintf(sh, SHELL_NORMAL, "IF[%d]: %s - WiFi=%d, Offloaded WiFi=%d\n", 
                      i, 
                      dev ? dev->name : "unknown",
                      net_if_is_wifi(all_iface),
                      net_off_is_wifi_offloaded(all_iface));
        i++;
    }
    
    /* Try to get the WiFi interface */
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
    
    /* Display IPv4 address if available */
    struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
    if (ipv4) {
        char addr_str[NET_IPV4_ADDR_LEN];
        
        if (net_ipv4_is_addr_unspecified(&ipv4->unicast[0].ipv4.address.in_addr)) {
            shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: Not assigned\n");
        } else {
            net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr, 
                         addr_str, sizeof(addr_str));
            shell_fprintf(sh, SHELL_NORMAL, "IPv4 address: %s\n", addr_str);
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
