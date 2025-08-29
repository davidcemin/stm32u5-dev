/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_SPI_H
#define EMW3080_SPI_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>

/* SPI communication functions for EMW3080B */

/* Initialize SPI communication */
int emw3080_spi_init(const struct device *spi_dev);

/* Send AT command over SPI */
int emw3080_spi_send_at_cmd(const struct device *spi_dev,
                           const char *cmd, size_t cmd_len,
                           char *resp_buf, size_t resp_len,
                           uint32_t timeout_ms);

/* Basic SPI transceive function */
int emw3080_spi_transceive(const struct device *spi_dev, 
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len);

#endif /* EMW3080_SPI_H */
