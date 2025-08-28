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
    
    /* Send data byte by byte with polling */
    for (size_t i = 0; i < len; i++) {
        /* Wait for TX buffer to be ready */
        int64_t start_time = k_uptime_get();
        bool tx_ready = false;
        
        while ((k_uptime_get() - start_time) < timeout_ms) {
            if (uart_irq_tx_ready(uart_dev)) {
                tx_ready = true;
                break;
            }
            /* Small delay to avoid tight loop */
            k_busy_wait(100);
        }
        
        if (!tx_ready) {
            LOG_ERR("UART TX timeout after %u bytes", (unsigned int)i);
            return -ETIMEDOUT;
        }
        
        /* Send byte */
        uart_poll_out(uart_dev, data[i]);
        
        /* Add small delay between characters for module to process */
        k_busy_wait(200);  /* 200 microseconds */
    }
    
    return len;
}

/* Function to flush the UART receive buffer */
void emw3080_uart_flush_rx(const struct device *uart_dev)
{
    uint8_t c;
    
    if (!uart_dev || !device_is_ready(uart_dev)) {
        return;
    }
    
    /* Read any pending data */
    while (uart_irq_rx_ready(uart_dev)) {
        uart_fifo_read(uart_dev, &c, 1);
    }
}
