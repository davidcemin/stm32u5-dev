/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_H_
#define ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_ip.h>

/* EMW3080 specific defines */
#define EMW3080_MAX_DATA_SIZE 2048
#define EMW3080_MAX_CONNECTIONS 5

/* AT command templates for WiFi operations */
extern const char *const emw3080_cmd_scan;
extern const char *const emw3080_cmd_connect;
extern const char *const emw3080_cmd_disconnect;

/* AT command templates for TCP/IP operations */
extern const char *const emw3080_cmd_start_tcp;
extern const char *const emw3080_cmd_start_udp;
extern const char *const emw3080_cmd_send_prepare;
extern const char *const emw3080_cmd_close;
extern const char *const emw3080_cmd_set_multi_conn;

/* Connection state for TCP/IP sockets */
struct emw3080_socket {
    bool in_use;
    uint8_t conn_id;
    uint16_t remote_port;
    uint8_t proto;  /* Protocol: IPPROTO_TCP or IPPROTO_UDP */
    char remote_ip[NET_IPV4_ADDR_LEN];
    struct k_sem sem;
};

/* UART receive buffer */
struct emw3080_uart_buffer {
    uint8_t data[EMW3080_MAX_DATA_SIZE];
    uint16_t len;
    bool data_ready;
    struct k_sem sem;
};

/* EMW3080 driver data structure */
struct emw3080_data {
    struct net_if *iface;
    const struct device *dev;
    struct gpio_dt_spec reset_gpio;
    struct gpio_dt_spec power_gpio;
    
    /* UART device for AT commands */
    const struct device *uart;
    struct k_work_q workq;
    struct k_work request_work;
    struct k_mutex uart_mutex;
    
    /* Connection state */
    bool connected;
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char passwd[WIFI_PSK_MAX_LEN + 1];
    
    /* IP configuration (for static or DHCP-acquired IP) */
    char local_ip[NET_IPV4_ADDR_LEN];
    char netmask[NET_IPV4_ADDR_LEN];
    char gateway[NET_IPV4_ADDR_LEN];
    
    /* Socket connections */
    struct emw3080_socket sockets[EMW3080_MAX_CONNECTIONS];
    
    /* UART buffer for receiving AT command responses */
    struct emw3080_uart_buffer rx_buf;
};

/* Network offloading API functions (implemented in emw3080_offload.c) */
extern const struct net_offload emw3080_offload;

/* Forward declaration for UART ISR function */
void emw3080_uart_isr(const struct device *uart, void *user_data);

/* Send AT command and wait for response */
int emw3080_send_at_cmd(struct emw3080_data *data, 
                      const char *cmd, size_t cmd_len,
                      char *resp_buf, size_t resp_len,
                      uint32_t timeout_ms);

/* Get the device pointer for EMW3080 driver */
const struct device *get_emw3080_device(void);

/* Initialize the EMW3080 with a specific UART device */
int emw3080_init_with_uart(const struct device *dev, const struct device *uart_dev);

#endif /* ZEPHYR_DRIVERS_WIFI_EMW3080_EMW3080_H_ */
