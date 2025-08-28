#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/offloaded_netdev.h>
#include <zephyr/net/socket.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include "../drivers/wifi/emw3080/emw3080_debug.h"

/* Forward declaration for function to get network device */
extern const struct device *get_emw3080_net_device(void);

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Helper function to get payload length since it's missing in newer Zephyr */
static inline size_t net_mgmt_event_get_payload_len(struct net_mgmt_event_callback *cb)
{
    return cb->info_length;
}

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback dhcp_cb;

/* Wi-Fi event handler for Zephyr v4.2.99 */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        LOG_INF("Wi-Fi connected");
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("Wi-Fi disconnected");
        break;
    case NET_EVENT_WIFI_SCAN_RESULT:
        {
            struct wifi_scan_result scan_result;
            if (net_mgmt_event_get_payload_len(cb) >= sizeof(scan_result)) {
                memcpy(&scan_result, cb->info, sizeof(scan_result));
                LOG_INF("Scan result: SSID: %-32s, RSSI: %d",
                    scan_result.ssid, scan_result.rssi);
            }
        }
        break;
    case NET_EVENT_WIFI_SCAN_DONE:
        LOG_INF("Scan complete");
        break;
    default:
        LOG_INF("Unhandled Wi-Fi event: %llu", mgmt_event);
        break;
    }
}

/* DHCP event handler */
static void dhcp_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event,
                               struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        char ip_addr[NET_IPV4_ADDR_LEN];
        struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

        if (!ipv4) {
            return;
        }

        net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr, 
                      ip_addr, sizeof(ip_addr));
        LOG_INF("DHCPv4 address acquired: %s", ip_addr);
    }
}

/* Helper function to get Wi-Fi interface */
static struct net_if *get_wifi_iface(void)
{
    struct net_if *iface = NULL;
    struct net_if *wifi_iface = NULL;
    struct net_if *any_iface = NULL;
    int count = 0;
    
    LOG_INF("Searching for network interfaces...");
    
    /* First pass - print all interfaces and identify EMW3080 */
    for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        iface = net_if_get_by_index(i);
        if (!iface) {
            continue;
        }
        
        count++;
        const struct device *dev = net_if_get_device(iface);
        
        /* Store the first interface as fallback */
        if (!any_iface) {
            any_iface = iface;
        }
        
        /* Try to determine if this is a WiFi interface based on capabilities */
        bool is_wifi = false;
        
        /* Check the device name for "wifi" or our EMW3080 */
        if (dev && dev->name) {
            if (strstr(dev->name, "wifi") != NULL || 
                strstr(dev->name, "WIFI") != NULL ||
                strstr(dev->name, "WiFi") != NULL) {
                is_wifi = true;
                if (!wifi_iface) {
                    wifi_iface = iface;
                }
            }
        }
        
        /* Log the interface info */
        LOG_INF("IF[%d]: %s, type=%s, mtu=%d", 
                i, dev ? dev->name : "unknown",
                is_wifi ? "WiFi" : "Other", 
                net_if_get_mtu(iface));
        
        /* Specifically look for our EMW3080 device */
        if (dev && dev->name && strstr(dev->name, "EMW3080") != NULL) {
            LOG_INF("Found EMW3080 interface: %d", i);
            return iface;  /* EMW3080 found - return immediately */
        }
    }

    /* No interfaces at all */
    if (count == 0) {
        LOG_ERR("No network interfaces found in the system");
        return NULL;
    }
    
    /* No EMW3080 found, but we have other WiFi interfaces we can use */
    if (wifi_iface) {
        LOG_WRN("No EMW3080 interface found, using generic WiFi interface");
        return wifi_iface;
    }
    
    LOG_WRN("No WiFi interface found, using first available interface");
    return any_iface;
}

int main(void)
{
    struct net_if *iface;
    LOG_INF("EMW3080 WiFi sample starting...");

    /* Debug: List all devices and interfaces for diagnostics */
    k_sleep(K_SECONDS(1));  /* Give devices time to initialize */
    LOG_INF("Running driver diagnostics");
    emw3080_debug_list_devices();
    emw3080_debug_list_interfaces();
    emw3080_debug_check_initialization();
    
    /* Try fallback initialization first, before checking for interfaces */
    LOG_INF("Trying fallback initialization...");
    extern int emw3080_fallback_init(void);
    emw3080_fallback_init();
    
    /* Check if our specialized network device is available */
    const struct device *net_dev = get_emw3080_net_device();
    if (net_dev && device_is_ready(net_dev)) {
        LOG_INF("Found EMW3080 network device: %s", net_dev->name);
    } else {
        LOG_WRN("EMW3080 network device not ready or not found");
    }
    
    /* Wait a bit for network interfaces to initialize */
    k_sleep(K_MSEC(500));
    
    /* Register for Wi-Fi network events */
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                NET_EVENT_WIFI_SCAN_RESULT |
                                NET_EVENT_WIFI_SCAN_DONE |
                                NET_EVENT_WIFI_CONNECT_RESULT |
                                NET_EVENT_WIFI_DISCONNECT_RESULT);

    net_mgmt_add_event_callback(&wifi_cb);

    /* Register for DHCP events */
    net_mgmt_init_event_callback(&dhcp_cb, dhcp_event_handler,
                                NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&dhcp_cb);

    /* Check network interfaces again after fallback init */
    iface = get_wifi_iface();
    if (!iface) {
        LOG_ERR("No network interfaces available");
        LOG_ERR("This might be due to:");
        LOG_ERR("1. Missing CONFIG_NET_NATIVE=y or CONFIG_NET_DRIVERS=y");
        LOG_ERR("2. Network driver not being registered properly");
        LOG_ERR("3. Insufficient interface slots (check CONFIG_NET_IF_MAX_IPV4_COUNT)");
        
        /* Print detailed config info to help debugging */
        LOG_INF("Network config status:");
        LOG_INF("- CONFIG_NETWORKING is %s", IS_ENABLED(CONFIG_NETWORKING) ? "enabled" : "disabled");
        LOG_INF("- CONFIG_NET_NATIVE is %s", IS_ENABLED(CONFIG_NET_NATIVE) ? "enabled" : "disabled");
        LOG_INF("- CONFIG_NET_OFFLOAD is %s", IS_ENABLED(CONFIG_NET_OFFLOAD) ? "enabled" : "disabled");
        LOG_INF("- CONFIG_NET_SOCKETS_OFFLOAD is %s", IS_ENABLED(CONFIG_NET_SOCKETS_OFFLOAD) ? "enabled" : "disabled");
        
        /* Create a dummy interface to allow shell commands to work */
        LOG_INF("Shell commands will still work for device diagnostics");
        LOG_INF("Try 'device list' to see all devices");
        LOG_INF("Try 'net iface' to check network interfaces");
    } else {
        const struct device *dev = net_if_get_device(iface);
        LOG_INF("Found network interface: %s", dev ? dev->name : "unknown");
        
        if (dev && dev->name && strstr(dev->name, "EMW3080") != NULL) {
            LOG_INF("EMW3080 WiFi interface ready!");
        } else {
            LOG_WRN("Using non-EMW3080 interface: %s", dev ? dev->name : "unknown");
            LOG_WRN("WiFi functionality may not work as expected");
        }
        
        LOG_INF("Use 'net' or 'wifi' shell commands to control the Wi-Fi interface");
    }
    
    return 0;
}
