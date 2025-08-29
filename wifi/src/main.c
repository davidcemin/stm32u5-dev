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
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/dhcpv4.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include "../drivers/wifi/emw3080/emw3080_debug.h"
#include "../drivers/wifi/emw3080/emw3080_test.h"

/* Forward declaration for function to get network device */
extern const struct device *get_emw3080_net_device(void);

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback dhcp_cb;

/* DHCP event handler */
static void dhcp_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event,
                               struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
        
        if (!ipv4) {
            LOG_ERR("No IPv4 configuration in interface");
            return;
        }
        
        /* Display IP address information */
        char ip_addr[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr,
                     ip_addr, sizeof(ip_addr));
                     
        LOG_INF("==== DHCP: Network configuration obtained ====");
        LOG_INF("  IPv4 Address: %s", ip_addr);
        
        /* The interface is now fully configured */
        LOG_INF("Network interface is ready for use!");
    }
}

/* Helper function to get payload length since it's missing in newer Zephyr */
static inline size_t net_mgmt_event_get_payload_len(struct net_mgmt_event_callback *cb)
{
    return cb->info_length;
}

/* Function to configure DHCP for an interface */
static void start_dhcp_for_interface(struct net_if *iface)
{
    if (!iface) {
        LOG_ERR("No valid interface for DHCP");
        return;
    }
    
    /* Check if interface is up */
    if (!net_if_is_up(iface)) {
        LOG_INF("Bringing interface up before starting DHCP");
        net_if_up(iface);
    }
    
    /* Start DHCP */
    LOG_INF("Starting DHCP for interface");
    net_dhcpv4_start(iface);
}

/* WiFi event handler */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
    struct wifi_scan_result scan_result;
    char ssid[WIFI_SSID_MAX_LEN + 1];
    
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        /* Get the interface and start DHCP when connected */
        LOG_INF("WiFi connected! Starting DHCP for IP configuration");
        start_dhcp_for_interface(iface);
        break;
        
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("WiFi disconnected - network connectivity lost");
        break;
        
    case NET_EVENT_WIFI_SCAN_RESULT:
        if (net_mgmt_event_get_payload_len(cb) >= sizeof(scan_result)) {
            memcpy(&scan_result, cb->info, sizeof(scan_result));
            
            /* Properly terminate the SSID for display */
            if (scan_result.ssid_length > WIFI_SSID_MAX_LEN) {
                scan_result.ssid_length = WIFI_SSID_MAX_LEN;
            }
            
            memcpy(ssid, scan_result.ssid, scan_result.ssid_length);
            ssid[scan_result.ssid_length] = '\0';
            
            /* Check if it's a hidden network */
            if (scan_result.ssid_length == 0) {
                LOG_INF("Scan: Hidden Network, Ch: %d, RSSI: %d",
                       scan_result.channel, scan_result.rssi);
            } else {
                LOG_INF("Scan: SSID: %s, Ch: %d, RSSI: %d",
                       ssid, scan_result.channel, scan_result.rssi);
            }
        }
        break;
        
    case NET_EVENT_WIFI_SCAN_DONE:
        LOG_INF("WiFi scan completed");
        break;
        
    default:
        LOG_DBG("Unhandled WiFi event: 0x%08llx", mgmt_event);
        break;
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
    
    /* Look for our specialized EMW3080_NET device first */
    const struct device *emw3080_net = get_emw3080_net_device();
    if (emw3080_net && device_is_ready(emw3080_net)) {
        LOG_INF("Found EMW3080 network device: %s", emw3080_net->name);
        
        /* Get the interface for this device */
        for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
            iface = net_if_get_by_index(i);
            if (!iface) {
                continue;
            }
            
            const struct device *dev = net_if_get_device(iface);
            if (dev == emw3080_net) {
                LOG_INF("Found EMW3080 network interface");
                return iface;
            }
        }
    }
    
    /* Fall back to scanning all interfaces */
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
                strstr(dev->name, "WiFi") != NULL ||
                strstr(dev->name, "EMW3080") != NULL) {
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

/* Display application banner with version */
static void print_app_banner(void)
{
    LOG_INF("****************************************");
    LOG_INF("*     EMW3080 WiFi Sample v1.0.0      *");
    LOG_INF("*  STM32U585 + EMW3080 WiFi Module    *");
    LOG_INF("****************************************");
    LOG_INF("Build: %s %s", __DATE__, __TIME__);

    /* Print basic system information */
    LOG_INF("System Information:");
    LOG_INF("- Zephyr Version: %s", KERNEL_VERSION_STRING);
    LOG_INF("- Board: %s", CONFIG_BOARD);
}

int main(void)
{
    struct net_if *iface;
    
    /* Print application banner */
    print_app_banner();

    /* Give devices time to initialize */
    LOG_INF("Starting with extended boot delay for safety");
    k_sleep(K_SECONDS(2));
    
    /* Delayed initialization to prevent boot issues */
    LOG_INF("Starting delayed EMW3080 initialization...");
    
    /* First, access only necessary functions to avoid crashing */
    LOG_INF("Running basic diagnostics...");
    emw3080_debug_list_devices();
    
    /* Wait a bit more for safety */
    k_sleep(K_MSEC(500));
    
    /* Continue with interface discovery */
    LOG_INF("Checking interfaces...");
    emw3080_debug_list_interfaces();
    
    /* Wait another moment */
    k_sleep(K_MSEC(500));
    
    /* Continue with more initialization */
    LOG_INF("Checking initialization status...");
    emw3080_debug_check_initialization();
    
    /* Try fallback initialization before checking for interfaces */
    LOG_INF("Attempting fallback initialization...");
    extern int emw3080_fallback_init(void);
    int ret = emw3080_fallback_init();
    if (ret == 0) {
        LOG_INF("Fallback initialization successful");
    } else {
        LOG_WRN("Fallback initialization returned: %d", ret);
    }
    
    /* Wait longer for network interfaces to initialize - 
     * safe mode takes more time to initialize */
    k_sleep(K_SECONDS(2));
    
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

    /* Check network interfaces after fallback init */
    LOG_INF("Searching for WiFi interface...");
    iface = get_wifi_iface();
    if (!iface) {
        LOG_ERR("ERROR: No network interfaces available!");
        LOG_ERR("This might be due to:");
        LOG_ERR("1. Missing CONFIG_NET_NATIVE=y or CONFIG_NET_DRIVERS=y");
        LOG_ERR("2. Network driver not being registered properly");
        LOG_ERR("3. Insufficient interface slots (check CONFIG_NET_IF_MAX_IPV4_COUNT)");
        LOG_ERR("4. Hardware connectivity issues - check UART4 connections");
        
        /* Print detailed config info to help debugging */
        LOG_INF("Network config status:");
        LOG_INF("- CONFIG_NETWORKING is %s", IS_ENABLED(CONFIG_NETWORKING) ? "enabled" : "disabled");
        LOG_INF("- CONFIG_NET_NATIVE is %s", IS_ENABLED(CONFIG_NET_NATIVE) ? "enabled" : "disabled");
        LOG_INF("- CONFIG_NET_OFFLOAD is %s", IS_ENABLED(CONFIG_NET_OFFLOAD) ? "enabled" : "disabled");
        LOG_INF("- CONFIG_NET_SOCKETS_OFFLOAD is %s", IS_ENABLED(CONFIG_NET_SOCKETS_OFFLOAD) ? "enabled" : "disabled");
    } else {
        const struct device *dev = net_if_get_device(iface);
        LOG_INF("Found network interface: %s", dev ? dev->name : "unknown");
        
        /* Check if this is our EMW3080 interface */
        bool is_emw3080 = false;
        if (dev && dev->name) {
            is_emw3080 = (strstr(dev->name, "EMW3080") != NULL ||
                          strstr(dev->name, "emw3080") != NULL);
        }
        
        if (is_emw3080) {
            LOG_INF("==== EMW3080 WiFi interface ready! ====");
            
            /* Initialize the EMW3080 L2 layer */
            LOG_INF("Initializing the EMW3080 L2 layer");
            extern void emw3080_l2_init(void);
            emw3080_l2_init();
            
            /* Attach L2 to interface */
            LOG_INF("Attaching EMW3080 L2 to interface");
            extern int emw3080_attach_l2_to_iface(struct net_if *iface);
            ret = emw3080_attach_l2_to_iface(iface);
            if (ret) {
                LOG_ERR("Failed to attach L2 to interface: %d", ret);
            }
            
            /* Configure IPv4 for the interface */
            LOG_INF("Setting up IPv4 configuration");
            
            /* Ensure the interface is up */
            if (!net_if_is_up(iface)) {
                LOG_INF("Bringing interface up");
                ret = net_if_up(iface);
                if (ret) {
                    LOG_ERR("Failed to bring interface up: %d", ret);
                }
            } else {
                LOG_INF("Interface is already up");
            }
            
            /* Configure IPv4 - use DHCP by default */
            LOG_INF("Starting DHCP client for automatic IP configuration");
            start_dhcp_for_interface(iface);
        } else {
            LOG_WRN("Using non-EMW3080 interface: %s", dev ? dev->name : "unknown");
            LOG_WRN("WiFi functionality may be limited");
            
            /* Still try to bring up the interface */
            if (!net_if_is_up(iface)) {
                LOG_INF("Bringing interface up");
                net_if_up(iface);
            }
        }
        
    /* Print usage instructions */
    LOG_INF("");
    LOG_INF("==== WiFi Shell Commands ====");
    LOG_INF("wifi scan                    - Scan for available networks");
    LOG_INF("wifi connect <SSID> <PSK>    - Connect to a WiFi network");
    LOG_INF("wifi disconnect              - Disconnect from current network");
    LOG_INF("wifi status                  - Show current WiFi status");
    LOG_INF("");
    LOG_INF("==== Network Commands ====");
    LOG_INF("net iface                    - Show network interfaces");
    LOG_INF("net ipv4                     - Show IPv4 addresses");
    
    /* Test our AT command functionality */
    LOG_INF("Testing AT commands...");
    extern int emw3080_debug_at_commands(void);
    ret = emw3080_debug_at_commands();
    if (ret == 0) {
        LOG_INF("AT command test successful");
    } else {
        LOG_ERR("AT command test failed: %d", ret);
    }

    LOG_INF("WiFi L2 implementation active");
}    LOG_INF("Initialization complete - WiFi sample is running");
    return 0;
}
