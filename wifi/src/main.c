#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/offloaded_netdev.h>
#include <zephyr/logging/log.h>
#include "../drivers/wifi/emw3080/emw3080_debug.h"

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
    int i = 0;
    struct net_if *first_iface = NULL;

    LOG_INF("Searching for network interfaces...");
    
    while ((iface = net_if_get_by_index(i)) != NULL) {
        /* Store the first interface as fallback */
        if (first_iface == NULL) {
            first_iface = iface;
        }
        
        /* Check if this interface has our driver */
        const struct device *dev = net_if_get_device(iface);
        LOG_INF("Interface %d: device = %s", i, dev ? dev->name : "NULL");
        
        if (dev != NULL && dev->name != NULL && strstr(dev->name, "EMW3080") != NULL) {
            LOG_INF("Found EMW3080 interface: %d", i);
            return iface;
        }
        i++;
    }

    if (i == 0) {
        LOG_ERR("No network interfaces found at all");
        return NULL;
    }

    LOG_WRN("No EMW3080 interface found, falling back to first available interface");
    return first_iface;
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

    /* Wait for network interface to be ready */
    iface = get_wifi_iface();
    if (!iface) {
        LOG_ERR("No Wi-Fi interfaces available from device tree binding");
        LOG_INF("Trying fallback initialization...");
        
        /* Try fallback initialization */
        extern int emw3080_fallback_init(void);
        int ret = emw3080_fallback_init();
        if (ret < 0) {
            LOG_ERR("Fallback initialization failed: %d", ret);
            LOG_INF("This is likely due to the EMW3080 driver not being registered properly.");
            LOG_INF("Check that:");
            LOG_INF("1. The device tree overlay for UART4 is correct");
            LOG_INF("2. The EMW3080 driver is properly registered in the system");
            return 0;
        }
        
        /* Try to get the interface again after fallback init */
        iface = get_wifi_iface();
        if (!iface) {
            LOG_ERR("No network interfaces found in the system");
            LOG_INF("This is likely due to a configuration issue with the network stack");
            LOG_INF("Check that CONFIG_NETWORKING=y and other required options are set");
            return 0;
        }
        
        const struct device *dev = net_if_get_device(iface);
        if (dev && dev->name && strstr(dev->name, "EMW3080") != NULL) {
            LOG_INF("Wi-Fi interface created through fallback initialization: %s", dev->name);
        } else {
            LOG_WRN("Using non-EMW3080 interface as fallback: %s", dev ? dev->name : "NULL");
            LOG_WRN("WiFi functionality may not work as expected");
        }
    } else {
        LOG_INF("Wi-Fi interface found through device tree binding");
    }
    
    LOG_INF("Use 'net' or 'wifi' shell commands to control the Wi-Fi interface");
    return 0;
}
