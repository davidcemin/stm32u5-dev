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

/* This is a fallback initialization function to create the EMW3080 driver
 * if the device tree binding isn't working. It's called from main.c after
 * the normal driver initialization has failed.
 */

/* Forward declarations from emw3080.c */
extern const struct net_wifi_mgmt_offload emw3080_api;
extern int emw3080_init_with_uart(const struct device *dev, const struct device *uart_dev);
extern void emw3080_register_net_if(const struct device *dev);

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
};

/* Static data and device instances for fallback */
static struct emw3080_data emw3080_fallback_data;

static const struct device emw3080_fallback_dev = {
    .name = "EMW3080_FALLBACK",
    .api = &emw3080_api,
    .data = &emw3080_fallback_data,
};

/* Implementation of the direct initialization function */
int emw3080_direct_init(const struct device *uart4)
{
    LOG_INF("EMW3080 fallback: Initializing with UART4: %s", uart4->name);
    
    /* Initialize the device with the UART */
    int ret = emw3080_init_with_uart(&emw3080_fallback_dev, uart4);
    if (ret < 0) {
        LOG_ERR("Failed to initialize EMW3080 with UART4: %d", ret);
        return ret;
    }
    
    /* Register network interface */
    emw3080_register_net_if(&emw3080_fallback_dev);
    
    /* Set a device name that will be recognized by our get_wifi_iface function */
    static char device_name_with_emw3080[] = "EMW3080_FALLBACK";
    ((struct device *)&emw3080_fallback_dev)->name = device_name_with_emw3080;
    
    /* Note: We cannot manually create a network interface here because
     * that requires using NET_DEVICE_INIT macros at compile time.
     * Instead, we will have our main.c code use any available network interface
     * if it can't find one specifically for the EMW3080. */
    
    LOG_INF("EMW3080 fallback initialization successful");
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
