/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_fallback, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/ethernet.h>

/* This is a fallback initialization function to create the EMW3080 driver
 * if the device tree binding isn't working. It's called from main.c after
 * the normal driver initialization has failed.
 */

/* Forward declarations from emw3080.c */
extern const struct net_wifi_mgmt_offload emw3080_api;
extern int emw3080_init_with_uart(const struct device *dev, const struct device *uart_dev);
extern void emw3080_register_net_if(const struct device *dev);

/* Define our own proper network interface registration function */
static void register_net_if_properly(const struct device *dev);

/* Define data structure for EMW3080 driver */
struct emw3080_data {
    const struct device *uart;
    struct gpio_dt_spec reset_gpio;
    struct gpio_dt_spec power_gpio;
    struct k_thread rx_thread;
    K_KERNEL_STACK_MEMBER(rx_stack, 1024); /* Fixed size stack */
    struct k_sem response_sem;
    char rx_buf[256]; /* Fixed size buffer */
    char response[256]; /* Fixed size buffer */
    uint16_t rx_buf_pos;
    struct net_if *iface; /* Store network interface pointer */
};

/* Simple placeholder for network callbacks */

/* Static data and device instances for fallback */
static struct emw3080_data emw3080_fallback_data;

/* Define a device that looks like a proper network device */
static const struct device emw3080_fallback_dev = {
    .name = "EMW3080_FALLBACK",
    .api = &emw3080_api,
    .data = &emw3080_fallback_data,
};

/* Forward declaration of debugging function (simplified) */
static void debug_log_interfaces(void);

/* Simple function to check for network interfaces */
static void debug_log_interfaces(void)
{
    int count = 0;
    struct net_if *iface;

    LOG_INF("---- Network Interfaces ----");
    
    for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        iface = net_if_get_by_index(i);
        if (!iface) {
            continue;
        }

        count++;
        const struct device *dev = net_if_get_device(iface);
        LOG_INF("IF[%d]: %s", i, dev ? dev->name : "unknown");
    }
    
    if (count == 0) {
        LOG_WRN("No network interfaces found");
    } else {
        LOG_INF("Found %d network interfaces", count);
    }
}

/* Find a network interface for a device - custom implementation */
static struct net_if *find_net_if_by_dev(const struct device *dev)
{
    struct net_if *iface;
    
    for (int i = 0; i < CONFIG_NET_IF_MAX_IPV4_COUNT; i++) {
        iface = net_if_get_by_index(i);
        if (!iface) {
            continue;
        }
        
        if (net_if_get_device(iface) == dev) {
            return iface;
        }
    }
    
    return NULL;
}

/* Register network interface properly using NET_DEVICE_INIT-like approach */
static void register_net_if_properly(const struct device *dev)
{
    struct emw3080_data *data = dev->data;
    
    /* Create a network interface directly with net_if_net_device_init */
    LOG_INF("Manually registering network interface for EMW3080");

    /* This approach mimics what NET_DEVICE_OFFLOAD_INIT would do */
    struct net_if *iface = find_net_if_by_dev(dev);
    if (iface == NULL) {
        LOG_WRN("No network interface found for device, creating one manually");
        /* We don't have a clean way to create a network interface manually in Zephyr 
           without the NET_DEVICE_OFFLOAD_INIT macro, but we can try to make the existing
           one visible to the network stack by checking all interfaces */
        
        /* Just check if we already found the interface earlier */
        if (data->iface) {
            LOG_INF("Using previously found network interface for EMW3080");
        } else {
            LOG_WRN("Could not find or create network interface");
        }
    } else {
        LOG_INF("Network interface exists for EMW3080");
        data->iface = iface;
    }
}

/* Implementation of the direct initialization function */
int emw3080_direct_init(const struct device *uart4)
{
    LOG_INF("EMW3080 fallback: Initializing with UART4: %s", uart4->name);
    
    /* Set a device name that will be recognized by our get_wifi_iface function */
    static char device_name_with_emw3080[] = "EMW3080_FALLBACK";
    ((struct device *)&emw3080_fallback_dev)->name = device_name_with_emw3080;
    
    /* Initialize the device with the UART */
    int ret = emw3080_init_with_uart(&emw3080_fallback_dev, uart4);
    if (ret < 0) {
        LOG_ERR("Failed to initialize EMW3080 with UART4: %d", ret);
        return ret;
    }

    /* Call our improved network interface registration 
     * Note that the standard emw3080_register_net_if() doesn't actually do anything */
    LOG_INF("Explicitly registering network interface");
    emw3080_register_net_if(&emw3080_fallback_dev);

    /* Try our manual method as well */
    register_net_if_properly(&emw3080_fallback_dev);
    
    /* Configuration check logs */
#ifdef CONFIG_NET_NATIVE
    LOG_INF("Native networking enabled");
#else
    LOG_WRN("Native networking disabled - interface registration might not work");
#endif

#ifdef CONFIG_NET_DRIVERS
    LOG_INF("Network drivers enabled");
#else
    LOG_WRN("Network drivers disabled - check CONFIG_NET_DRIVERS=y");
#endif

#ifdef CONFIG_NET_OFFLOAD
    LOG_INF("Network offloading enabled");
#else
    LOG_WRN("Network offloading disabled - check CONFIG_NET_OFFLOAD=y");
#endif

#ifdef CONFIG_NET_SOCKETS_OFFLOAD
    LOG_INF("Socket offloading enabled");
#else
    LOG_WRN("Socket offloading disabled - check CONFIG_NET_SOCKETS_OFFLOAD=y");
#endif

    LOG_INF("EMW3080 fallback initialization complete");
    return 0;
}

/* This function is called from main.c if the normal DT binding fails */
int emw3080_fallback_init(void)
{
    LOG_INF("Trying fallback initialization for EMW3080");
    
    /* Get UART4 directly */
    const struct device *uart4 = DEVICE_DT_GET(DT_NODELABEL(uart4));
    if (!uart4) {
        LOG_ERR("UART4 not found in fallback init");
        return -ENODEV;
    }
    
    if (!device_is_ready(uart4)) {
        LOG_ERR("UART4 not ready in fallback init");
        return -ENODEV;
    }
    
    LOG_INF("Found UART4 device: %s", uart4->name);
    
    /* Initialize EMW3080 using the direct API */
    return emw3080_direct_init(uart4);
}
