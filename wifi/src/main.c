#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback dhcp_cb;

/* Wi-Fi event handler updated for Zephyr v4.2.99 */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,  /* Changed from uint32_t to uint64_t */
                                    struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_IPV4_ADDR_ADD:  /* Simplified events since we don't have WiFi specific ones */
        LOG_INF("Network connected");
        break;
    case NET_EVENT_IPV4_ADDR_DEL:
        LOG_INF("Network disconnected");
        break;
    default:
        LOG_INF("Unhandled network event: %llu", mgmt_event);
        break;
    }
}

/* DHCP event handler */
static void dhcp_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event,  /* Changed from uint32_t to uint64_t */
                               struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        LOG_INF("DHCPv4 address acquired!");
    }
}

int main(void)
{
    LOG_INF("Network sample starting...");

    /* Register for IPv4 network events */
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_IPV4_ADDR_ADD |
                                 NET_EVENT_IPV4_ADDR_DEL);

    net_mgmt_add_event_callback(&wifi_cb);

    /* Register for DHCP events */
    net_mgmt_init_event_callback(&dhcp_cb, dhcp_event_handler,
                                 NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&dhcp_cb);

    return 0;
}
