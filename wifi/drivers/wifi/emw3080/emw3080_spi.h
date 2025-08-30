/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_SPI_H
#define EMW3080_SPI_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include "emw3080_slip.h"

/* EMW3080B SPI Protocol Constants - Based on MX WiFi Library */
#define EMW3080_SPI_WRITE           0x0A
#define EMW3080_SPI_READ            0x0B
#define EMW3080_SPI_STATUS_CMD      0x04    /* Status query command */
#define EMW3080_SPI_HEADER_SIZE     5       /* MX WiFi HCI: type + len + lenx */
#define EMW3080_SPI_MAX_PAYLOAD_SIZE 2048
#define EMW3080_SPI_MAX_DATA_SIZE   2048    /* Maximum data size for transactions */
#define EMW3080_SPI_MAX_FRAME_SIZE  (EMW3080_SPI_HEADER_SIZE + EMW3080_SPI_MAX_PAYLOAD_SIZE)

/* Status register bits */
#define EMW3080_SPI_STATUS_READY    0x01  /* Device ready (0 = ready) */
#define EMW3080_SPI_STATUS_DATA_AVAILABLE 0x02  /* Data available to read */
#define EMW3080_SPI_STATUS_BUSY     0x04  /* Device busy processing */

/* EMW3080B MX WiFi Compatible SPI Frame Header */
struct emw3080_spi_header {
    uint8_t type;       /* Command type (0x0A = write, 0x0B = read) */
    uint16_t len;       /* Payload length (little-endian) */
    uint16_t lenx;      /* Payload length XOR'd with 0xFFFF */
} __packed;

/* SPI communication functions for EMW3080B - MX WiFi Compatible */

/* Initialize SPI communication */
int emw3080_spi_init(const struct device *spi_dev);

/* MX WiFi compatible SPI functions */
int emw3080_spi_send_frame(const struct device *spi_dev,
                          const uint8_t *data, size_t data_len);

int emw3080_spi_recv_frame(const struct device *spi_dev,
                          uint8_t *data, size_t max_len, size_t *received_len);

/* Basic SPI transceive function */
int emw3080_spi_transceive(const struct device *spi_dev, 
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len);

/* Wait for EMW3080 to be ready */
int emw3080_spi_wait_ready(const struct device *spi_dev, uint32_t timeout_ms);

/* Full-duplex SPI functions following ST's pattern */
int emw3080_spi_full_duplex_transaction(const struct device *spi_dev,
                                       const uint8_t *tx_data, size_t tx_len,
                                       uint8_t *rx_data, size_t rx_max_len, size_t *rx_len);

int emw3080_spi_send_frame_duplex(const struct device *spi_dev,
                                 const uint8_t *data, size_t data_len);

int emw3080_spi_recv_frame_duplex(const struct device *spi_dev,
                                 uint8_t *data, size_t max_len, size_t *received_len);

int emw3080_spi_send_recv_frame(const struct device *spi_dev,
                               const uint8_t *tx_data, size_t tx_len,
                               uint8_t *rx_data, size_t rx_max_len, size_t *rx_len);

struct spi_dt_spec; /* forward declaration to avoid heavy header in public API */
int emw3080_spi_set_dt_spec(const struct spi_dt_spec *spec);

#endif /* EMW3080_SPI_H */
