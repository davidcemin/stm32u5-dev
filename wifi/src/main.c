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
#include <zephyr/net/net_l2.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_offload.h>
#include <string.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include "../drivers/wifi/emw3080/emw3080_debug.h"
#include "../drivers/wifi/emw3080/emw3080_test.h"
#include "../drivers/wifi/emw3080/emw3080_offload.h"

/* Forward declaration for function to get network device */
extern const struct device *get_emw3080_net_device(void);
/* EMW3080 test function declarations */
extern int slip_validation_test(void);
extern int emw3080_spi_basic_test(void);
extern int emw3080_spi_init_basic(void);

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
                
                /* Check if offload API is properly set up by NET_DEVICE_OFFLOAD_INIT */
                bool is_offloaded = net_if_is_offloaded(iface);
                LOG_INF("EMW3080: Interface offload status: %s", is_offloaded ? "ENABLED" : "DISABLED");
                
                if (is_offloaded) {
                    const struct net_offload *offload_api = net_if_offload(iface);
                    if (offload_api && offload_api->send) {
                        LOG_INF("EMW3080: Offload API properly initialized - send: %p", offload_api->send);
                    } else {
                        LOG_WRN("EMW3080: Offload enabled but API not available");
                    }
                } else {
                    LOG_WRN("EMW3080: Interface not marked as offloaded - check NET_DEVICE_OFFLOAD_INIT");
                }
                
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
    
    /* Bottom-up testing: SPI → SLIP validation */
    LOG_INF("Starting bottom-up testing: SPI → SLIP layers...");
    
    /* Step 1: Bring up SPI interface if needed */
    LOG_INF("Step 1: Initializing SPI interface...");
    extern int emw3080_spi_init_basic(void);  // We'll need to create this function
    int ret = emw3080_spi_init_basic();
    if (ret == 0) {
        LOG_INF("✅ SPI interface initialized successfully");
    } else {
        LOG_ERR("❌ SPI interface initialization failed: %d", ret);
        goto cleanup;
    }
    
    /* Step 2: Test SPI interface */
    LOG_INF("Step 2: Testing SPI interface...");
    extern int emw3080_spi_basic_test(void);
    ret = emw3080_spi_basic_test();
    if (ret == 0) {
        LOG_INF("✅ SPI interface test PASSED");
    } else {
        LOG_ERR("❌ SPI interface test FAILED: %d", ret);
        goto cleanup;
    }
    
    /* Step 3: Bring up SLIP if needed */
    LOG_INF("Step 3: Initializing SLIP protocol...");
    // SLIP is a protocol layer, no specific initialization needed beyond what's in the test
    LOG_INF("✅ SLIP protocol ready (stateless protocol)");
    
    /* Step 4: Test SLIP */
    LOG_INF("Step 4: Testing SLIP protocol...");
    ret = slip_validation_test();
    if (ret == 0) {
        LOG_INF("✅ SLIP protocol test PASSED");
    } else {
        LOG_ERR("❌ SLIP protocol test FAILED: %d", ret);
        goto cleanup;
    }
    
    LOG_INF("🎉 Bottom-up validation completed successfully!");
    LOG_INF("SPI ✅ | SLIP ✅ | Ready for next layer integration");

cleanup:
    LOG_INF("Bottom-up testing complete - ready for shell commands");
    return 0;
}
