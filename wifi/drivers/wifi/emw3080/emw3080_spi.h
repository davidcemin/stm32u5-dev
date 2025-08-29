/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_SPI_H
#define EMW3080_SPI_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>

/* EMW3080B SPI Protocol Constants */
#define EMW3080_SPI_MAGIC_WRITE     0x02
#define EMW3080_SPI_MAGIC_READ      0x03
#define EMW3080_SPI_STATUS_CMD      0x04
#define EMW3080_SPI_HEADER_SIZE     4
#define EMW3080_SPI_MAX_PAYLOAD_SIZE 2048
#define EMW3080_SPI_MAX_FRAME_SIZE  (EMW3080_SPI_HEADER_SIZE + EMW3080_SPI_MAX_PAYLOAD_SIZE)

/* Status register bits */
#define EMW3080_SPI_STATUS_READY    0x01  /* Device ready (0 = ready) */
#define EMW3080_SPI_STATUS_DATA_AVAILABLE 0x02  /* Data available to read */
#define EMW3080_SPI_STATUS_BUSY     0x04  /* Device busy processing */

/* Response types */
#define EMW3080_RESP_OK             "OK"
#define EMW3080_RESP_ERROR          "ERROR"
#define EMW3080_RESP_FAIL           "FAIL"
#define EMW3080_RESP_READY          "ready"
#define EMW3080_RESP_PROMPT         ">"

/* Response parsing results */
enum emw3080_response_type {
    EMW3080_RESP_TYPE_UNKNOWN = 0,
    EMW3080_RESP_TYPE_OK,
    EMW3080_RESP_TYPE_ERROR,
    EMW3080_RESP_TYPE_DATA,
    EMW3080_RESP_TYPE_TIMEOUT,
    EMW3080_RESP_TYPE_INCOMPLETE,
    EMW3080_RESP_TYPE_NOTIFICATION
};

/* Response parsing structure */
struct emw3080_response {
    enum emw3080_response_type type;
    char *data;
    size_t data_len;
    int error_code;
    bool complete;
};

/* EMW3080B SPI Frame Header */
struct emw3080_spi_header {
    uint8_t magic;      /* Magic byte (0x02 for write, 0x03 for read) */
    uint8_t reserved;   /* Reserved byte */
    uint16_t length;    /* Payload length (little-endian) */
} __packed;

/* SPI communication functions for EMW3080B */

/* Initialize SPI communication */
int emw3080_spi_init(const struct device *spi_dev);

/* Enhanced AT command interface with response parsing */
int emw3080_spi_send_at_cmd_enhanced(const struct device *spi_dev,
                                   const char *cmd, size_t cmd_len,
                                   struct emw3080_response *response,
                                   uint32_t timeout_ms);

/* Parse AT response into structured format */
enum emw3080_response_type emw3080_parse_response(const char *data, size_t len,
                                                struct emw3080_response *response);

/* Check if response is complete (ends with OK/ERROR/etc) */
bool emw3080_response_complete(const char *data, size_t len);

/* Send AT command over SPI using EMW3080B protocol (legacy) */
int emw3080_spi_send_at_cmd(const struct device *spi_dev,
                           const char *cmd, size_t cmd_len,
                           char *resp_buf, size_t resp_len,
                           uint32_t timeout_ms);

/* Low-level SPI frame functions */
int emw3080_spi_send_frame(const struct device *spi_dev,
                          const uint8_t *data, size_t data_len);

int emw3080_spi_recv_frame(const struct device *spi_dev,
                          uint8_t *data, size_t max_len, size_t *received_len);

/* Basic SPI transceive function */
int emw3080_spi_transceive(const struct device *spi_dev, 
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len);

#endif /* EMW3080_SPI_H */
