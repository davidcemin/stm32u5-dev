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
    
    /* Initialize the semaphores and mutex */
    k_mutex_init(&data->uart_mutex);
    k_sem_init(&data->rx_buf.sem, 0, 1);
    
    /* Initialize socket structures */
    for (int i = 0; i < EMW3080_MAX_CONNECTIONS; i++) {
        data->sockets[i].in_use = false;
        data->sockets[i].conn_id = i;
        data->sockets[i].proto = IPPROTO_UDP; /* Default to UDP */
        k_sem_init(&data->sockets[i].sem, 0, 1);
    }
    
    /* Set up UART callback for data reception */
    uart_irq_callback_user_data_set(data->uart, emw3080_uart_isr, (void *)dev);
    
    /* Enable UART receive interrupt */
    uart_irq_rx_enable(data->uart);
    
    /* The uart_irq_is_enabled function doesn't exist in Zephyr's API
     * so we'll just trust that the interrupt is enabled after our call
     */
    LOG_INF("UART IRQ should be enabled now");
    
    /* Double-check by trying to enable it again (safe to call multiple times) */
    uart_irq_rx_enable(data->uart);
    LOG_INF("UART IRQ enabled again for good measure");
    
    /* Send AT command to test communication */
    char resp[64];
    int ret = emw3080_send_at_cmd(data, "AT\r\n", 4, resp, sizeof(resp), 1000);
    if (ret < 0) {
        LOG_ERR("Failed to communicate with EMW3080 module: %d", ret);
        /* Continue anyway, but this indicates a problem */
    } else {
        LOG_INF("EMW3080 module responded to AT command");
    }
    
    /* Enable multi-connection mode */
    LOG_INF("Enabling multi-connection mode");
    char cmd[32];
    snprintf(cmd, sizeof(cmd), emw3080_cmd_set_multi_conn, 1); /* 1 = multiple connections */
    ret = emw3080_send_at_cmd(data, cmd, strlen(cmd), resp, sizeof(resp), 2000);
    if (ret < 0) {
        LOG_WRN("Failed to enable multi-connection mode: %d", ret);
    } else {
        LOG_INF("Multi-connection mode enabled");
    }
    
    /* Initialize the WiFi management interface */
    emw3080_mgmt_init();
    
    /* Initialize L2 networking */
    LOG_INF("Initializing EMW3080 L2 interface");
    extern void emw3080_l2_init(void);
    emw3080_l2_init();
    
    /* Set WiFi connection state */
    data->connected = false;
    
    LOG_INF("EMW3080 driver initialized successfully");
    return 0;
}

/* UART ISR implementation */
void emw3080_uart_isr(const struct device *uart, void *user_data)
{
    const struct device *dev = (const struct device *)user_data;
    struct emw3080_data *data = dev->data;
    static bool ipd_mode = false;
    static int ipd_conn_id = -1;
    static int ipd_len = 0;
    static int ipd_data_read = 0;
    
    if (!uart_irq_update(uart)) {
        return;
    }
    
    if (uart_irq_rx_ready(uart)) {
        uint8_t c;
        int bytes_read = 0;
        
        /* Debug counter to limit log messages */
        static int debug_counter = 0;
        debug_counter++;
        
        /* Read while data is available */
        while (uart_fifo_read(uart, &c, 1) == 1) {
            bytes_read++;
            
            /* Simple buffer protection */
            if (data->rx_buf.len < EMW3080_MAX_DATA_SIZE - 1) {
                data->rx_buf.data[data->rx_buf.len++] = c;
                
                /* Print every 10th byte for debugging purposes */
                if (debug_counter % 10 == 0) {
                    LOG_DBG("UART RX (%d): 0x%02x (%c)", 
                           data->rx_buf.len, c, (c >= 32 && c <= 126) ? c : '.');
                }
                
                /* Process character by character for IPD data */
                if (ipd_mode) {
                    ipd_data_read++;
                    /* Check if we've read all data */
                    if (ipd_data_read >= ipd_len) {
                        LOG_INF("IPD data complete: conn=%d, len=%d", ipd_conn_id, ipd_len);
                        
                        /* Process the received data packet */
                        emw3080_process_ipd(data, data->rx_buf.data, data->rx_buf.len);
                        
                        /* Reset IPD mode */
                        ipd_mode = false;
                        ipd_conn_id = -1;
                        ipd_len = 0;
                        ipd_data_read = 0;
                        
                        /* Reset buffer */
                        data->rx_buf.len = 0;
                    }
                } 
                /* Check for standard AT response termination */
                else if (data->rx_buf.len >= 4 && 
                    ((data->rx_buf.len >= 4 && memcmp(&data->rx_buf.data[data->rx_buf.len - 4], "OK\r\n", 4) == 0) ||
                     (data->rx_buf.len >= 7 && memcmp(&data->rx_buf.data[data->rx_buf.len - 7], "ERROR\r\n", 7) == 0) ||
                     (data->rx_buf.len >= 9 && memcmp(&data->rx_buf.data[data->rx_buf.len - 9], "SEND OK\r\n", 9) == 0))) {
                    
                    LOG_INF("AT response complete, detected terminator");
                    LOG_INF("Response: '%.*s'", 
                           data->rx_buf.len > 30 ? 30 : data->rx_buf.len, 
                           data->rx_buf.data);
                    
                    /* Mark data as ready and signal waiting thread */
                    data->rx_buf.data_ready = true;
                    k_sem_give(&data->rx_buf.sem);
                }
                /* Check for +IPD start sequence */
                else if (!ipd_mode && data->rx_buf.len >= 5) {
                    /* Look for +IPD sequence in the last 20 bytes */
                    int search_start = (data->rx_buf.len > 20) ? (data->rx_buf.len - 20) : 0;
                    int search_len = data->rx_buf.len - search_start;
                    
                    /* Create temporary null-terminated string for search */
                    char temp[21]; /* Max 20 bytes + null terminator */
                    if (search_len > 20) {
                        search_len = 20;
                    }
                    memcpy(temp, &data->rx_buf.data[search_start], search_len);
                    temp[search_len] = '\0';
                    
                    char *ipd_str = strstr(temp, "+IPD,");
                    if (ipd_str) {
                        LOG_INF("Found +IPD sequence");
                        
                        /* +IPD sequence found, now try to parse connection ID and length */
                        int conn_id, length;
                        if (sscanf(ipd_str, "+IPD,%d,%d:", &conn_id, &length) == 2) {
                            LOG_INF("IPD message detected: conn=%d, len=%d", conn_id, length);
                            
                            /* Find the start of data after the colon */
                            char *data_start = strchr(ipd_str, ':');
                            if (data_start) {
                                data_start++; /* Move past the colon */
                                
                                /* Calculate absolute position in buffer */
                                int offset = (data_start - temp) + search_start;
                                
                                /* Calculate how much data we've already read */
                                int already_read = data->rx_buf.len - offset;
                                
                                /* Enter IPD mode */
                                ipd_mode = true;
                                ipd_conn_id = conn_id;
                                ipd_len = length;
                                ipd_data_read = already_read;
                                
                                LOG_INF("IPD already read: %d of %d bytes", already_read, length);
                                
                                /* If we've already read all the data */
                                if (already_read >= length) {
                                    LOG_INF("IPD data complete (immediate)");
                                    
                                    /* Process the received data packet */
                                    emw3080_process_ipd(data, &data->rx_buf.data[offset - 5], data->rx_buf.len - (offset - 5));
                                    
                                    /* Reset IPD mode */
                                    ipd_mode = false;
                                    ipd_conn_id = -1;
                                    ipd_len = 0;
                                    ipd_data_read = 0;
                                    
                                    /* Reset buffer */
                                    data->rx_buf.len = 0;
                                }
                            }
                        }
                    }
                }
            } else {
                /* Buffer overflow, reset */
                data->rx_buf.len = 0;
                LOG_ERR("UART buffer overflow");
                
                /* Reset IPD mode */
                ipd_mode = false;
                ipd_conn_id = -1;
                ipd_len = 0;
                ipd_data_read = 0;
            }
        }
        
        /* Log summary of bytes read */
        if (bytes_read > 0 && debug_counter % 10 == 0) {
            LOG_DBG("Read %d bytes in one ISR call", bytes_read);
        }
    }
}

/* Implementation of send AT command function declared in header */
int emw3080_send_at_cmd(struct emw3080_data *data, 
                      const char *cmd, size_t cmd_len,
                      char *resp_buf, size_t resp_len,
                      uint32_t timeout_ms)
{
    int ret;
    
    if (!data || !data->uart) {
        LOG_ERR("Invalid data or UART device not available");
        return -EINVAL;
    }
    
    if (!device_is_ready(data->uart)) {
        LOG_ERR("UART device not ready for AT commands");
        return -ENODEV;
    }
    
    LOG_INF("Sending AT command (len=%d, timeout=%dms): %.*s", 
           cmd_len, timeout_ms, cmd_len, cmd);
    
    /* Lock UART access */
    k_mutex_lock(&data->uart_mutex, K_FOREVER);
    
    /* Reset buffer */
    data->rx_buf.len = 0;
    data->rx_buf.data_ready = false;
    
    /* Make sure UART IRQ is enabled */
    uart_irq_rx_enable(data->uart);
    
    /* Send command */
    LOG_DBG("Writing command to UART...");
    for (size_t i = 0; i < cmd_len; i++) {
        uart_poll_out(data->uart, cmd[i]);
        /* Small delay to ensure reliable transmission */
        k_busy_wait(100);
    }
    
    LOG_DBG("Waiting for response with timeout %d ms", timeout_ms);
    
    /* Wait for response with timeout */
    ret = k_sem_take(&data->rx_buf.sem, K_MSEC(timeout_ms));
    
    if (ret == 0 && data->rx_buf.data_ready) {
        LOG_DBG("Received response (len=%d)", data->rx_buf.len);
        
        /* Copy response if buffer provided */
        if (resp_buf && resp_len > 0) {
            size_t copy_len = data->rx_buf.len < resp_len ? data->rx_buf.len : resp_len - 1;
            memcpy(resp_buf, data->rx_buf.data, copy_len);
            resp_buf[copy_len] = '\0';
            LOG_DBG("Response: %s", resp_buf);
        }
        
        /* Check for success/error */
        if (data->rx_buf.len >= 4 && 
            memcmp(&data->rx_buf.data[data->rx_buf.len - 4], "OK\r\n", 4) == 0) {
            LOG_DBG("Command successful (found OK)");
            ret = 0;  /* Success */
        } else {
            LOG_WRN("Command error response: %.*s", 
                   data->rx_buf.len > 20 ? 20 : data->rx_buf.len,
                   data->rx_buf.data);
            ret = -EIO;  /* Error */
        }
    } else {
        LOG_ERR("Command timed out after %d ms", timeout_ms);
        /* Dump any partial data we might have received */
        if (data->rx_buf.len > 0) {
            LOG_WRN("Partial response (%d bytes): %.*s", 
                   data->rx_buf.len,
                   data->rx_buf.len > 20 ? 20 : data->rx_buf.len,
                   data->rx_buf.data);
        }
        ret = -ETIMEDOUT;  /* Timeout */
    }
    
    /* Unlock UART access */
    k_mutex_unlock(&data->uart_mutex);
    
    return ret;
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
    /* This is just a placeholder function. The actual interface registration
     * is handled by the Zephyr networking stack when using the proper
     * DEVICE_DEFINE/NET_DEVICE_OFFLOAD_INIT macros. Since we're creating
     * the device manually, we can't use those macros here.
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
