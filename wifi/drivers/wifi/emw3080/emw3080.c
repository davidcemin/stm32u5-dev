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
#include "emw3080_mgmt.h" /* Include the management header for function declarations */
#include "emw3080_socket.h" /* Include the socket header for process_ipd function */
#include "emw3080_uart.h" /* Include the UART header for UART functions */
#include "emw3080_hw.h" /* Include the hardware header for HW functions */

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

/* Forward declaration of the work handler */
static void emw3080_request_handler(struct k_work *work);

/* Driver initialization - SAFE BOOT version */
static int emw3080_init(const struct device *dev)
{
    struct emw3080_data *data = dev->data;
    data->dev = dev;

    LOG_INF("Initializing EMW3080 WiFi driver in SAFE MODE [%s]", dev->name);
    
    /* Minimal initialization to prevent boot issues */
    
    /* Initialize the semaphores and mutex */
    k_mutex_init(&data->uart_mutex);
    k_sem_init(&data->rx_buf.sem, 0, 1);
    
    /* Initialize socket structures - basic initialization only */
    for (int i = 0; i < EMW3080_MAX_CONNECTIONS; i++) {
        data->sockets[i].in_use = false;
        data->sockets[i].conn_id = i;
        data->sockets[i].proto = IPPROTO_UDP;
        k_sem_init(&data->sockets[i].sem, 0, 1);
    }
    
    /* Store UART device but don't configure it yet */
    if (data->uart) {
        LOG_INF("UART device from DT binding: %s", data->uart->name);
    } else {
        const struct device *uart4 = device_get_binding("uart4");
        if (uart4) {
            LOG_INF("Using UART4: %s", uart4->name);
            data->uart = uart4;
        }
    }
    
    /* Set WiFi connection state */
    data->connected = false;
    
    /* We'll do the actual initialization later, on first use */
    LOG_INF("EMW3080 driver initialized in SAFE MODE - actual init will be done later");
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

/* Implementation of send AT command function declared in header */
int emw3080_send_at_cmd(struct emw3080_data *data, 
                      const char *cmd, size_t cmd_len,
                      char *resp_buf, size_t resp_len,
                      uint32_t timeout_ms)
{
    int ret;
    int retry_count = 3; // Number of retries for AT commands
    
    if (!data || !data->uart) {
        LOG_ERR("Invalid data or UART device not available");
        return -EINVAL;
    }
    
    if (!device_is_ready(data->uart)) {
        LOG_ERR("UART device not ready for AT commands");
        return -ENODEV;
    }
    
    LOG_INF("Sending AT command (len=%d, timeout=%dms): %.*s", 
           cmd_len, timeout_ms, (int)cmd_len, cmd);
    
    while (retry_count-- > 0) {
        /* Lock UART access */
        k_mutex_lock(&data->uart_mutex, K_FOREVER);
        
        /* Reset buffer */
        data->rx_buf.len = 0;
        data->rx_buf.data_ready = false;
        
        /* Flush RX buffer before sending */
        emw3080_uart_flush_rx(data->uart);
        
        /* Send command using direct polling */
        LOG_DBG("Writing command to UART (attempt %d)...", 3 - retry_count);
        
        /* Direct send each character with poll_out - more reliable */
        for (size_t i = 0; i < cmd_len; i++) {
            uart_poll_out(data->uart, cmd[i]);
            k_busy_wait(500); /* 500us delay between chars */
        }
        
        /* Wait a bit for any immediate responses */
        k_sleep(K_MSEC(10));
        
        /* Now poll for response with timeout */
        uint32_t start_time = k_uptime_get_32();
        uint32_t end_time = start_time + timeout_ms;
        bool got_ok = false;
        bool got_error = false;
        
        /* Read response with polling instead of interrupts */
        while (k_uptime_get_32() < end_time && !got_ok && !got_error) {
            uint8_t c;
            
            /* Check if there's data available */
            if (uart_poll_in(data->uart, &c) == 0) {
                /* Got a character, add to buffer */
                if (data->rx_buf.len < EMW3080_MAX_DATA_SIZE - 1) {
                    data->rx_buf.data[data->rx_buf.len++] = c;
                    
                    /* Check for OK response */
                    if (data->rx_buf.len >= 4 && 
                        memcmp(&data->rx_buf.data[data->rx_buf.len - 4], "OK\r\n", 4) == 0) {
                        LOG_INF("Found OK response");
                        got_ok = true;
                        break;
                    }
                    
                    /* Check for ERROR response */
                    if (data->rx_buf.len >= 7 && 
                        memcmp(&data->rx_buf.data[data->rx_buf.len - 7], "ERROR\r\n", 7) == 0) {
                        LOG_WRN("Found ERROR response");
                        got_error = true;
                        break;
                    }
                } else {
                    /* Buffer overflow, reset */
                    LOG_ERR("Buffer overflow");
                    data->rx_buf.len = 0;
                }
            } else {
                /* No data available, sleep a bit */
                k_sleep(K_MSEC(10));
            }
        }
        
        /* Copy response if buffer provided */
        if (resp_buf && resp_len > 0 && data->rx_buf.len > 0) {
            size_t copy_len = data->rx_buf.len < resp_len ? data->rx_buf.len : resp_len - 1;
            memcpy(resp_buf, data->rx_buf.data, copy_len);
            resp_buf[copy_len] = '\0';
            LOG_DBG("Response: %s", resp_buf);
        }
        
        /* Check results */
        if (got_ok) {
            LOG_DBG("Command successful");
            k_mutex_unlock(&data->uart_mutex);
            return 0; /* Success */
        } else if (got_error) {
            LOG_WRN("Command returned error");
            k_mutex_unlock(&data->uart_mutex);
            return -EIO; /* Error */
        } else if (data->rx_buf.len > 0) {
            /* Got partial response but no OK/ERROR */
            LOG_WRN("Partial response: %.*s", 
                   data->rx_buf.len > 40 ? 40 : data->rx_buf.len,
                   data->rx_buf.data);
                   
            /* Check if there's "OK" anywhere in the response */
            for (size_t i = 0; i < data->rx_buf.len - 1; i++) {
                if (data->rx_buf.data[i] == 'O' && data->rx_buf.data[i+1] == 'K') {
                    LOG_INF("Found OK in response");
                    k_mutex_unlock(&data->uart_mutex);
                    return 0; /* Success */
                }
            }
            
            k_mutex_unlock(&data->uart_mutex);
            
            if (retry_count > 0) {
                LOG_WRN("Retrying command after delay...");
                k_sleep(K_MSEC(100));
                continue;
            }
            return -EIO;
        } else {
            LOG_ERR("Command timed out");
            k_mutex_unlock(&data->uart_mutex);
            
            if (retry_count > 0) {
                LOG_WRN("Retrying command after timeout...");
                k_sleep(K_MSEC(100));
                continue;
            }
            return -ETIMEDOUT; /* Timeout */
        }
    }
    
    /* If we get here, all retries failed */
    LOG_ERR("Failed after all retries");
    return -EIO;
}

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
static void emw3080_request_handler(struct k_work *work)
{
    struct emw3080_data *data = CONTAINER_OF(work, struct emw3080_data, request_work);
    char resp[64];
    
    /* Process queued requests - example: check connection status */
    if (emw3080_send_at_cmd(data, "AT+CIPSTATUS\r\n", 13, resp, sizeof(resp), 1000) == 0) {
        /* Process connection status response */
        LOG_INF("Connection status: %s", resp);
    }
}

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
int emw3080_init_with_uart(const struct device *dev, const struct device *uart_dev)
{
    int ret;
    struct emw3080_data *data = dev->data;
    
    /* Store the UART device pointer */
    data->uart = uart_dev;
    
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
    
    LOG_INF("Performing delayed initialization of EMW3080 WiFi driver");
    
    /* Initialize GPIO pins if available */
    if (data->reset_gpio.port) {
        LOG_INF("Configuring reset GPIO");
        if (!gpio_is_ready_dt(&data->reset_gpio)) {
            LOG_ERR("Reset GPIO device not ready");
        } else {
            gpio_pin_configure_dt(&data->reset_gpio, GPIO_OUTPUT_INACTIVE);
        }
    }
    
    if (data->power_gpio.port) {
        LOG_INF("Configuring power GPIO");
        if (!gpio_is_ready_dt(&data->power_gpio)) {
            LOG_ERR("Power GPIO device not ready");
        } else {
            gpio_pin_configure_dt(&data->power_gpio, GPIO_OUTPUT_INACTIVE);
        }
    }
    
    /* Configure UART */
    if (data->uart && device_is_ready(data->uart)) {
        LOG_INF("Configuring UART: %s", data->uart->name);
        
        /* Configure the UART with proper settings */
        struct uart_config uart_cfg = {
            .baudrate = 115200,
            .parity = UART_CFG_PARITY_NONE,
            .stop_bits = UART_CFG_STOP_BITS_1,
            .data_bits = UART_CFG_DATA_BITS_8,
            .flow_ctrl = UART_CFG_FLOW_CTRL_NONE
        };
        
        /* Apply configuration */
        int ret = uart_configure(data->uart, &uart_cfg);
        if (ret != 0) {
            LOG_ERR("Failed to configure UART: %d", ret);
        }
        
        /* Ensure all interrupts are disabled */
        uart_irq_rx_disable(data->uart);
        uart_irq_tx_disable(data->uart);
        
        /* Clear any pending data */
        uint8_t c;
        while (uart_poll_in(data->uart, &c) == 0) {
            /* Discard the character */
        }
    } else {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }
    
    /* Try a hardware reset */
    LOG_INF("Attempting hardware reset");
    if (data->reset_gpio.port) {
        LOG_INF("Using GPIO reset");
        gpio_pin_set_dt(&data->reset_gpio, 1); /* Assert reset (high) */
        k_sleep(K_MSEC(200));                 /* Hold in reset */
        gpio_pin_set_dt(&data->reset_gpio, 0); /* Release reset (low) */
        k_sleep(K_SECONDS(1));                /* Allow module to boot */
    }
    
    /* Initialize networking components */
    LOG_INF("Initializing network components");
    emw3080_mgmt_init();
    extern void emw3080_l2_init(void);
    emw3080_l2_init();
    
    LOG_INF("EMW3080 delayed initialization complete");
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
