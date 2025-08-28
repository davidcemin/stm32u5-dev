/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_UART_H
#define EMW3080_UART_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>

/* Configure UART with proper settings for EMW3080 module */
int emw3080_configure_uart(const struct device *uart_dev);

/* Low-level UART send function with error checking */
int emw3080_uart_send_data(const struct device *uart_dev, 
                         const uint8_t *data, size_t len,
                         uint32_t timeout_ms);

/* Function to flush the UART receive buffer */
void emw3080_uart_flush_rx(const struct device *uart_dev);

#endif /* EMW3080_UART_H */
