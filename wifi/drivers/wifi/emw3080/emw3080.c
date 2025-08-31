/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT mxchip_emw3080 
/* Note: This must match the compatible string "mxchip,emw3080" in device tree 
 * but with commas replaced by underscores */

#include <zephyr/logging/log.h>
#ifndef CONFIG_EMW3080_LOG_LEVEL
#define CONFIG_EMW3080_LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#endif
LOG_MODULE_REGISTER(emw3080, CONFIG_EMW3080_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_offload.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "emw3080.h"
#include "emw3080_spi.h"

#include "emw3080.h"
#include "emw3080_spi.h"
#include "emw3080_mgmt.h" /* Include the management header for function declarations */
#include "emw3080_socket.h" /* Include the socket header for process_ipd function */
#include "emw3080_uart.h" /* Include the UART header for UART functions */
#include "emw3080_hw.h" /* Include the hardware header for HW functions */
#include "emw3080_ipc.h" /* Include the IPC header for binary protocol functions */

/* EMW3080 specific defines */
#define EMW3080_MAX_DATA_SIZE 2048
#define EMW3080_MAX_CONNECTIONS 5
#define EMW3080_CONNECT_TIMEOUT K_SECONDS(10)
#define EMW3080_CMD_TIMEOUT K_SECONDS(2)

/* AT command templates for WiFi operations */
const char *const emw3080_cmd_scan = "AT+SCAN\r\n";
const char *const emw3080_cmd_connect = "AT+CWJAP=\"%s\",\"%s\"\r\n";
const char *const emw3080_cmd_disconnect = "AT+CWQAP\r\n";

/* AT command templates for TCP/IP operations */
const char *const emw3080_cmd_start_tcp = "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n";
const char *const emw3080_cmd_start_udp = "AT+CIPSTART=\"UDP\",\"%s\",%d\r\n";
const char *const emw3080_cmd_send_prepare = "AT+CIPSEND=%d,%d\r\n";  /* connection_id, length */
const char *const emw3080_cmd_close = "AT+CIPCLOSE=%d\r\n";
const char *const emw3080_cmd_set_multi_conn = "AT+CIPMUX=%d\r\n";    /* 0=single, 1=multi */

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
    
    LOG_INF("EMW3080 network interface initialization (iface=%p, index=%d)",
           iface, net_if_get_by_iface(iface));
    
    data->iface = iface;
    
    /* Set MAC address (for now using a fixed address) */
    uint8_t mac[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    net_if_set_link_addr(iface, mac, sizeof(mac), NET_LINK_ETHERNET);
    
    LOG_INF("Set MAC address: %02x:%02x:%02x:%02x:%02x:%02x", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    /* Make sure the management interface knows about this interface */
    emw3080_mgmt_set_iface(iface);
    
    /* Configure L2 for this interface */
    extern int emw3080_attach_l2_to_iface(struct net_if *iface);
    int ret = emw3080_attach_l2_to_iface(iface);
    if (ret < 0) {
        LOG_ERR("Failed to attach L2 to interface: %d", ret);
    } else {
        LOG_INF("Successfully attached L2 to interface");
    }
    
    /* Enable direct packet mode */
    extern int emw3080_enable_direct_mode(struct net_if *iface);
    ret = emw3080_enable_direct_mode(iface);
    if (ret < 0) {
        LOG_ERR("Failed to enable direct mode: %d", ret);
    }
    
    /* Set the interface UP and RUNNING */
    net_if_flag_set(iface, NET_IF_UP);
    net_if_flag_set(iface, NET_IF_RUNNING);
    
    LOG_INF("EMW3080 network interface initialized (UP=%d, RUNNING=%d)",
           net_if_flag_is_set(iface, NET_IF_UP),
           net_if_flag_is_set(iface, NET_IF_RUNNING));
}

/* Driver initialization - SPI version */
static int emw3080_init(const struct device *dev)
{
    struct emw3080_data *data = dev->data;
    data->dev = dev;

    LOG_INF("Initializing EMW3080 WiFi driver with SPI [%s]", dev->name);
    
    /* Initialize the semaphores and mutex */
    k_mutex_init(&data->spi_mutex);
    k_sem_init(&data->rx_buf.sem, 0, 1);
    
    /* Initialize socket structures - basic initialization only */
    for (int i = 0; i < EMW3080_MAX_CONNECTIONS; i++) {
        data->sockets[i].in_use = false;
        data->sockets[i].conn_id = i;
        data->sockets[i].proto = IPPROTO_UDP;
        k_sem_init(&data->sockets[i].sem, 0, 1);
    }
    
    /* Basic HW bring-up: power/reset before SPI init */
    if (data->power_gpio.port && gpio_is_ready_dt(&data->power_gpio)) {
        /* Drive to active state for enable (respect active-low) */
        gpio_pin_configure_dt(&data->power_gpio, GPIO_OUTPUT_INACTIVE);
        int active = (data->power_gpio.dt_flags & GPIO_ACTIVE_LOW) ? 0 : 1;
        gpio_pin_set_dt(&data->power_gpio, active);
        LOG_INF("EMW3080 power enabled (active=%d)", active);
        k_msleep(50);
    }
    if (data->reset_gpio.port && gpio_is_ready_dt(&data->reset_gpio)) {
        /* Configure output, start in inactive state */
        gpio_pin_configure_dt(&data->reset_gpio, GPIO_OUTPUT_INACTIVE);
        int assert_level = (data->reset_gpio.dt_flags & GPIO_ACTIVE_LOW) ? 0 : 1;
        int release_level = assert_level ? 0 : 1;
        /* Assert reset */
        gpio_pin_set_dt(&data->reset_gpio, assert_level);
        k_msleep(10);
        /* Release reset */
        gpio_pin_set_dt(&data->reset_gpio, release_level);
    /* Allow module to boot firmware */
    k_msleep(1500);
        LOG_INF("EMW3080 hardware reset completed (assert=%d release=%d)", assert_level, release_level);
    }

    /* Get and store SPI device */
    if (data->spi) {
        LOG_INF("SPI device from DT binding: %s", data->spi->name);
        /* Apply DT SPI config (freq, CS) */
    /* Use default SPI mode from DT (no CPOL/CPHA flags here = Mode 0 unless DT adds them) */
    const struct spi_dt_spec spec = SPI_DT_SPEC_GET(
    DT_DRV_INST(0),
    SPI_WORD_SET(8) | SPI_TRANSFER_MSB
#ifdef CONFIG_EMW3080_SPI_MODE3
    | SPI_MODE_CPOL | SPI_MODE_CPHA
#endif
    ,
    0);
    (void)emw3080_spi_set_dt_spec(&spec);
        
        /* Initialize SPI communication */
    /* Configure FLOW pin in SPI helper */
    (void)emw3080_spi_set_flow_gpio(&data->wake_gpio);
    int ret = emw3080_spi_init(data->spi);
        if (ret == 0) {
            LOG_INF("SPI communication initialized successfully");
            
            /* Initialize binary IPC protocol only (no commands here) */
            ret = emw3080_ipc_init(dev);
            if (ret != 0) {
                LOG_ERR("IPC protocol initialization failed: %d", ret);
                return ret;
            }
            LOG_INF("EMW3080 IPC protocol initialized successfully");
        } else {
            LOG_WRN("SPI communication init failed: %d", ret);
        }
    } else {
        LOG_ERR("No SPI device available");
        return -ENODEV;
    }
    
    /* Set WiFi connection state */
    data->connected = false;
    
    LOG_INF("EMW3080 driver initialized with SPI");
    return 0;
}

/* UART ISR implementation */
/* UART ISR completely disabled to prevent boot issues.
 * We'll use polling for UART communications instead. */
void emw3080_uart_isr(const struct device *uart, void *user_data)
{
    /* Do nothing in the ISR - disabled to prevent hanging */
    return;
}

/* Implementation of send AT command function - DEPRECATED: AT commands replaced by MIPC */
int emw3080_send_at_cmd(struct emw3080_data *data, 
                      const char *cmd, size_t cmd_len,
                      char *resp_buf, size_t resp_len,
                      uint32_t timeout_ms)
{
    LOG_ERR("AT commands are no longer supported - use MIPC protocol instead");
    return -ENOTSUP;
}

/* Status check for EMW3080B */
/* Function to parse IP address from response - used in multiple places */
void emw3080_parse_ip_info(struct emw3080_data *data, char *resp)
{
    /* Parse IP information from response - simplified implementation */
    char *ip_str = strstr(resp, "IP:");
    if (ip_str) {
        ip_str += 3;  /* Skip "IP:" */
        int i = 0;
        while (*ip_str && *ip_str != '\r' && *ip_str != '\n' && i < NET_IPV4_ADDR_LEN - 1) {
            data->local_ip[i++] = *ip_str++;
        }
        data->local_ip[i] = '\0';
        LOG_INF("IP address: %s", data->local_ip);
    }
}

/* Work queue handler for asynchronous processing */

/* Get the type of offloaded network interface */
static enum offloaded_net_if_types emw3080_get_type(void)
{
    return L2_OFFLOADED_NET_IF_TYPE_WIFI;
}

/* WiFi management operation implementations - now using emw3080_mgmt.c functions */
static int emw3080_scan(const struct device *dev, 
                       struct wifi_scan_params *params,
                       scan_result_cb_t cb)
{
    LOG_INF("EMW3080 scan operation - forwarding to emw3080_mgmt_scan");
    
    /* Make sure we have the interface set properly */
    struct emw3080_data *data = dev->data;
    if (data && data->iface) {
        emw3080_mgmt_set_iface(data->iface);
        LOG_INF("Setting interface %p for scan operation", data->iface);
    } else {
        LOG_WRN("No interface found in device data, using default");
    }
    
    /* Forward to the management implementation */
    return emw3080_mgmt_scan(dev, params, cb);
}

static int emw3080_connect(const struct device *dev, 
                          struct wifi_connect_req_params *params)
{
    LOG_INF("EMW3080 connect operation - forwarding to emw3080_mgmt_connect");
    /* Forward to the management implementation */
    return emw3080_mgmt_connect(dev, params);
}

static int emw3080_disconnect(const struct device *dev)
{
    LOG_INF("EMW3080 disconnect operation - forwarding to emw3080_mgmt_disconnect");
    /* Forward to the management implementation */
    return emw3080_mgmt_disconnect(dev);
}

static const struct wifi_mgmt_ops emw3080_mgmt_ops = {
    .scan = emw3080_scan,
    .connect = emw3080_connect,
    .disconnect = emw3080_disconnect,
    /* No get_status in this version of Zephyr's wifi_mgmt_ops structure */
};

/* Define driver data and config */
#define EMW3080_INIT(inst)                                                     \
    static struct emw3080_data emw3080_data_##inst = {                          \
        .reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),         \
        .power_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, power_gpios, {0}),         \
    .wake_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, wakeup_gpios, {0}),         \
        /* Get the SPI device from the device tree */                           \
        .spi = DEVICE_DT_GET(DT_INST_BUS(inst)),                                \
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

/* Function to get the EMW3080 device - required by other modules */
const struct device *get_emw3080_device(void)
{
    /* Get the first EMW3080 device instance */
    #if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
    return DEVICE_DT_INST_GET(0);
    #else
    /* Fallback for cases where the device tree entry isn't found */
    LOG_ERR("No EMW3080 device found in device tree");
    return NULL;
    #endif
}

/* Include hardware functions */
#include "emw3080_hw.h"

/* Functions to be called from emw3080_fallback.c */
int emw3080_init_with_spi(const struct device *dev, const struct device *spi_dev)
{
    int ret;
    struct emw3080_data *data = dev->data;
    
    /* Store the SPI device pointer */
    data->spi = spi_dev;
    
    /* Initialize hardware first */
    ret = emw3080_hw_init(data);
    if (ret != 0) {
        LOG_ERR("Hardware initialization failed: %d", ret);
        /* Continue anyway, the init function has fallbacks */
    }
    
    /* Call the regular init function */
    return emw3080_init(dev);
}

void emw3080_register_net_if(const struct device *dev)
{
    /* This is just a placeholder function. The actual interface registration
     * is handled by the Zephyr networking stack when using the proper
     * DEVICE_DEFINE/NET_DEVICE_OFFLOAD_INIT macros. Since we're creating
     * the device manually, we can't use those macros here.
     */
    LOG_INF("Network interface registered for EMW3080");
}

/* Safe delayed initialization function that will be called from main.c 
 * after the system is fully booted */
int emw3080_delayed_init(void)
{
    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("EMW3080 device not found");
        return -ENODEV;
    }
    
    struct emw3080_data *data = dev->data;
    if (!data) {
        LOG_ERR("EMW3080 data not initialized");
        return -EINVAL;
    }
    
    LOG_INF("Delayed init requested (noop by default)");
    
    /* No operation: the device is already initialized in emw3080_init() */
    return 0;
}

/* This is a simplified init function that will be called directly from main.c 
 * The actual implementation is in emw3080_fallback.c, this is just an alternative
 * implementation that would be used if we didn't have the fallback code.
 */
/*
int emw3080_fallback_init(void)
{
    return emw3080_delayed_init();
}
*/

/* Debug message to show driver is being compiled */
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
BUILD_ASSERT(1, "EMW3080 driver being compiled!");
#else
#warning "No compatible EMW3080 node found in device tree!"
#endif

DT_INST_FOREACH_STATUS_OKAY(EMW3080_INIT)
