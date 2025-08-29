/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_hw, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include "emw3080.h"
#include "emw3080_uart.h"

/* Hardware control functions for EMW3080 module */

/* Perform a hardware reset of the EMW3080 module if possible */
int emw3080_hw_reset(struct emw3080_data *data)
{
    if (!data) {
        return -EINVAL;
    }

    LOG_INF("Performing hardware reset of EMW3080 module");

    /* Check if we have a reset GPIO */
    if (data->reset_gpio.port != NULL) {
        LOG_INF("Reset via GPIO pin");
        
        /* Pull reset pin LOW (active) */
        gpio_pin_set_dt(&data->reset_gpio, 1);
        
        /* Hold for reset pulse duration */
        k_sleep(K_MSEC(100));
        
        /* Release reset pin (inactive) */
        gpio_pin_set_dt(&data->reset_gpio, 0);
        
        /* Wait for module to initialize - EMW3080 needs significant time after reset */
        LOG_INF("Waiting for module to initialize after reset...");
        k_sleep(K_SECONDS(2));
        
        return 0;
    } 
    else {
        LOG_WRN("No reset GPIO available, trying software reset");
        
        /* Try software reset via AT command if we have a UART */
        if (data->uart && device_is_ready(data->uart)) {
            /* Configure UART first */
            emw3080_configure_uart(data->uart);
            
            /* Ensure UART IRQ is enabled */
            uart_irq_rx_enable(data->uart);
            
            /* Flush any pending data */
            emw3080_uart_flush_rx(data->uart);
            
            /* Send AT+RST command directly using low-level function to avoid recursion */
            const char *rst_cmd = "AT+RST\r\n";
            LOG_INF("Sending software reset command: AT+RST");
            
            /* Send reset command */
            int ret = emw3080_uart_send_data(data->uart, (const uint8_t *)rst_cmd, 
                                          strlen(rst_cmd), 1000);
            if (ret < 0) {
                LOG_ERR("Failed to send software reset command: %d", ret);
                return ret;
            }
            
            /* Wait for module to reset and initialize */
            LOG_INF("Waiting for module to initialize after software reset...");
            k_sleep(K_SECONDS(3));
            
            /* Flush any initialization messages */
            emw3080_uart_flush_rx(data->uart);
            
            return 0;
        } else {
            LOG_ERR("No reset GPIO and no UART available, cannot reset module");
            return -ENODEV;
        }
    }
}

/* Initialize the EMW3080 hardware */
int emw3080_hw_init(struct emw3080_data *data)
{
    int ret;
    
    if (!data || !data->uart) {
        LOG_ERR("Invalid data or no UART device");
        return -EINVAL;
    }
    
    /* Configure the UART with proper settings */
    ret = emw3080_configure_uart(data->uart);
    if (ret != 0) {
        LOG_ERR("Failed to configure UART: %d", ret);
        return ret;
    }
    
    /* Reset the module */
    ret = emw3080_hw_reset(data);
    if (ret != 0) {
        LOG_WRN("Hardware reset failed: %d", ret);
        /* Continue anyway - the module might still work */
    }
    
    /* Disable UART IRQs - we'll use polling instead to avoid boot issues */
    uart_irq_rx_disable(data->uart);
    uart_irq_tx_disable(data->uart);
    
    /* Clear any pending data */
    emw3080_uart_flush_rx(data->uart);
    
    /* Initialize buffer state */
    data->rx_buf.len = 0;
    data->rx_buf.data_ready = false;
    
    LOG_INF("UART configured for polling mode (no interrupts)");
    
    LOG_INF("EMW3080 hardware initialized successfully");
    return 0;
}
