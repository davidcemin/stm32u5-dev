/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mxchip_emw3080 
/* Note: This must match the compatible string "mxchip,emw3080" in device tree 
 * but with commas replaced by underscores */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "emw3080.h"

/* EMW3080 specific defines */
#define EMW3080_MAX_DATA_SIZE 2048
#define EMW3080_MAX_CONNECTIONS 5
#define EMW3080_CONNECT_TIMEOUT K_SECONDS(10)
#define EMW3080_CMD_TIMEOUT K_SECONDS(2)

/* AT command templates */
static const char *const emw3080_cmd_scan = "AT+SCAN\r\n";
static const char *const emw3080_cmd_connect = "AT+CWJAP=\"%s\",\"%s\"\r\n";
static const char *const emw3080_cmd_disconnect = "AT+CWQAP\r\n";

struct emw3080_data {
    struct net_if *iface;
    const struct device *dev;
    struct gpio_dt_spec reset_gpio;
    struct gpio_dt_spec power_gpio;
    
    /* UART device for AT commands */
    const struct device *uart;
    struct k_work_q workq;
    struct k_work request_work;
    
    /* Connection state */
    bool connected;
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char passwd[WIFI_PSK_MAX_LEN + 1];
};

/* Forward declarations */
static void emw3080_iface_init(struct net_if *iface);
static enum offloaded_net_if_types emw3080_get_type(void);
static const struct wifi_mgmt_ops emw3080_mgmt_ops;

/* Define the API structure */
const struct net_wifi_mgmt_offload emw3080_api = {
    .wifi_iface.iface_api.init = emw3080_iface_init,
    .wifi_iface.get_type = emw3080_get_type,
    .wifi_mgmt_api = &emw3080_mgmt_ops,
};

/* Initialize network interface */
static void emw3080_iface_init(struct net_if *iface)
{
    const struct device *dev = net_if_get_device(iface);
    struct emw3080_data *data = dev->data;
    
    data->iface = iface;
    
    /* Set MAC address (for now using a fixed address) */
    uint8_t mac[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    net_if_set_link_addr(iface, mac, sizeof(mac), NET_LINK_ETHERNET);
    
    LOG_INF("EMW3080 network interface initialized");
}

/* Forward declaration of the work handler */
static void emw3080_request_handler(struct k_work *work);

/* Driver initialization */
static int emw3080_init(const struct device *dev)
{
    struct emw3080_data *data = dev->data;
    data->dev = dev;

    LOG_INF("Initializing EMW3080 WiFi driver [%s]", dev->name);
    LOG_INF("Checking if UART4 device exists directly");
    const struct device *uart4 = device_get_binding("uart4");
    if (uart4) {
        LOG_INF("Found UART4: %s (ready: %d)", uart4->name, device_is_ready(uart4));
    } else {
        LOG_ERR("UART4 not found using device_get_binding()");
    }
    
    /* Initialize GPIO pins if available */
    if (data->reset_gpio.port) {
        LOG_INF("Reset GPIO found: port=%p, pin=%d", 
               data->reset_gpio.port, data->reset_gpio.pin);
        if (!gpio_is_ready_dt(&data->reset_gpio)) {
            LOG_ERR("Reset GPIO device not ready");
            return -ENODEV;
        }
        gpio_pin_configure_dt(&data->reset_gpio, GPIO_OUTPUT_INACTIVE);
        LOG_INF("Reset GPIO configured");
    } else {
        LOG_WRN("No reset GPIO specified in device tree");
    }
    
    if (data->power_gpio.port) {
        LOG_INF("Power GPIO found: port=%p, pin=%d", 
               data->power_gpio.port, data->power_gpio.pin);
        if (!gpio_is_ready_dt(&data->power_gpio)) {
            LOG_ERR("Power GPIO device not ready");
            return -ENODEV;
        }
        gpio_pin_configure_dt(&data->power_gpio, GPIO_OUTPUT_INACTIVE);
        LOG_INF("Power GPIO configured");
    }
    
    /* Initialize UART */
    if (data->uart) {
        LOG_INF("UART device from DT binding: %s", data->uart->name);
        if (!device_is_ready(data->uart)) {
            LOG_ERR("UART device not ready");
            /* Try to use uart4 directly if available */
            if (uart4 && device_is_ready(uart4)) {
                LOG_INF("Using UART4 directly instead");
                data->uart = uart4;
            } else {
                return -ENODEV;
            }
        }
        LOG_INF("UART device ready");
    } else {
        LOG_ERR("No UART device found in data structure");
        /* Try to use uart4 directly if available */
        if (uart4 && device_is_ready(uart4)) {
            LOG_INF("Using UART4 directly instead");
            data->uart = uart4;
        } else {
            return -ENODEV;
        }
    }
    
    /* Reset the module */
    LOG_INF("Resetting EMW3080 module");
    if (data->reset_gpio.port) {
        gpio_pin_set_dt(&data->reset_gpio, 1);
        k_sleep(K_MSEC(100));
        gpio_pin_set_dt(&data->reset_gpio, 0);
        k_sleep(K_SECONDS(2)); /* Allow module to boot */
        LOG_INF("Reset sequence completed");
    }
    
    /* Initialize work queue for async operations */
    k_work_init(&data->request_work, emw3080_request_handler);
    
    LOG_INF("EMW3080 driver initialized successfully");
    return 0;
}

/* Work queue handler (placeholder) */
static void emw3080_request_handler(struct k_work *work)
{
    /* Handle queued work items (to be implemented) */
}

/* Get the type of offloaded network interface */
static enum offloaded_net_if_types emw3080_get_type(void)
{
    return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

/* WiFi management operation implementations */
static int emw3080_scan(const struct device *dev, 
                       struct wifi_scan_params *params,
                       scan_result_cb_t cb)
{
    LOG_INF("EMW3080 scan operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_connect(const struct device *dev, 
                          struct wifi_connect_req_params *params)
{
    LOG_INF("EMW3080 connect operation (not yet implemented)");
    return -ENOTSUP;
}

static int emw3080_disconnect(const struct device *dev)
{
    LOG_INF("EMW3080 disconnect operation (not yet implemented)");
    return -ENOTSUP;
}

static const struct wifi_mgmt_ops emw3080_mgmt_ops = {
    .scan = emw3080_scan,
    .connect = emw3080_connect,
    .disconnect = emw3080_disconnect,
};

/* Define driver data and config */
#define EMW3080_INIT(inst)                                                     \
    static struct emw3080_data emw3080_data_##inst = {                          \
        .reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),         \
        .power_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, power_gpios, {0}),         \
        /* Try to get the UART device from the device tree */                   \
        .uart = DEVICE_DT_GET(DT_INST_BUS(inst)),                               \
    };                                                                          \
                                                                               \
    DEVICE_DT_INST_DEFINE(inst,                                                 \
                         &emw3080_init,                                         \
                         NULL,                                                  \
                         &emw3080_data_##inst,                                  \
                         NULL,                                                  \
                         POST_KERNEL,                                           \
                         CONFIG_WIFI_INIT_PRIORITY,                             \
                         &emw3080_api);

/* Declaration for fallback function that will be implemented in emw3080_fallback.c */
int emw3080_direct_init(const struct device *uart4);

/* Functions to be called from emw3080_fallback.c */
int emw3080_init_with_uart(const struct device *dev, const struct device *uart_dev)
{
    struct emw3080_data *data = dev->data;
    data->uart = uart_dev;
    
    /* Call the regular init function */
    return emw3080_init(dev);
}

void emw3080_register_net_if(const struct device *dev)
{
    /* This function is a stub as we can't directly call net_if functions
     * without proper network driver integration.
     * The fallback driver will be detected by the get_wifi_iface() function in main.c
     * because we set the device name to include "EMW3080"
     */
    LOG_INF("Network interface registered for EMW3080");
}

/* Debug message to show driver is being compiled */
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
BUILD_ASSERT(1, "EMW3080 driver being compiled!");
#else
#warning "No compatible EMW3080 node found in device tree!"
#endif

DT_INST_FOREACH_STATUS_OKAY(EMW3080_INIT)
