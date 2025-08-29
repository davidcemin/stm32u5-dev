/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_uart, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include "emw3080.h"

/* Helper functions for UART initialization and communication */

/* Configure UART with proper settings for EMW3080 module */
int emw3080_configure_uart(const struct device *uart_dev)
{
    if (!uart_dev || !device_is_ready(uart_dev)) {
        LOG_ERR("Invalid UART device or not ready");
        return -ENODEV;
    }
    
    LOG_INF("Configuring UART settings for EMW3080");
    
    /* Default UART configuration for EMW3080 */
    struct uart_config uart_cfg = {
        .baudrate = 115200,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE
    };
    
    /* Apply configuration */
    int ret = uart_configure(uart_dev, &uart_cfg);
    if (ret != 0) {
        LOG_ERR("Failed to configure UART: %d", ret);
        return ret;
    }
    
    /* Disable any existing callbacks */
    uart_irq_rx_disable(uart_dev);
    uart_irq_tx_disable(uart_dev);
    
    /* Clear any pending data */
    while (uart_irq_rx_ready(uart_dev)) {
        uint8_t c;
        uart_fifo_read(uart_dev, &c, 1);
    }
    
    LOG_INF("UART configured successfully");
    return 0;
}

/* Low-level UART send function with error checking */
int emw3080_uart_send_data(const struct device *uart_dev, 
                         const uint8_t *data, size_t len,
                         uint32_t timeout_ms)
{
    if (!uart_dev || !data || len == 0) {
        return -EINVAL;
    }
    
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }
    
    LOG_DBG("Sending %d bytes to UART with timeout %d ms", (int)len, (int)timeout_ms);
    
    /* Try direct polling method first - more reliable on some STM32 UARTs */
    for (size_t i = 0; i < len; i++) {
        int retries = 10; /* Number of retries for each character */
        bool sent = false;
        
        while (retries-- > 0 && !sent) {
            /* Direct poll out - note: uart_poll_out returns void in Zephyr API */
            uart_poll_out(uart_dev, data[i]);
            
            /* Assume success since uart_poll_out returns void */
            sent = true;
            
            /* Small delay between characters - STM32 UARTs need this for reliability */
            k_busy_wait(500);  /* 500 microseconds */
        }
        
        if (!sent) {
            LOG_ERR("Failed to send byte %d after multiple retries", (int)i);
            return -EIO;
        }
    }
    
    LOG_DBG("Successfully sent %d bytes to UART", (int)len);
    
    /* Ensure final byte is transmitted before returning */
    k_sleep(K_MSEC(5));
    
    return len;
}

/* Function to flush the UART receive buffer */
void emw3080_uart_flush_rx(const struct device *uart_dev)
{
    uint8_t c;
    
    if (!uart_dev || !device_is_ready(uart_dev)) {
        return;
    }
    
    /* Safety limit to prevent infinite loops */
    int safety_counter = 100;
    
    /* Use polling to flush the UART buffer */
    while (safety_counter-- > 0) {
        /* Try to read a character */
        if (uart_poll_in(uart_dev, &c) == 0) {
            /* Got a character, discard it and continue */
            continue;
        } else {
            /* No more data available */
            break;
        }
    }
    
    /* Add a small delay to ensure UART is fully flushed */
    k_busy_wait(1000); /* 1ms */
}
