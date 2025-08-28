#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/offloaded_netdev.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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

    while ((iface = net_if_get_by_index(i)) != NULL) {
        /* Check if this interface has our driver */
        const struct device *dev = net_if_get_device(iface);
        if (dev != NULL && strstr(dev->name, "EMW3080") != NULL) {
            return iface;
        }
        i++;
    }

    return NULL;
}

int main(void)
{
    struct net_if *iface;
    LOG_INF("EMW3080 WiFi sample starting...");

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
        LOG_ERR("No Wi-Fi interfaces available");
        return 0;
    }
    
    LOG_INF("Wi-Fi interface found. Use 'net' or 'wifi' shell commands to control.");
    return 0;
}
