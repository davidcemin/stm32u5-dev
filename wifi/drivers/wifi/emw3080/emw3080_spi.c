/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_spi, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>
#include "emw3080.h"
#include "emw3080_spi.h"

/* SPI configuration for EMW3080B */
static struct spi_config emw3080_spi_cfg = {
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
    .frequency = 8000000, /* 8 MHz - EMW3080B supports up to 20MHz */
    .slave = 0,
};

/* Mutex for SPI access synchronization */
static K_MUTEX_DEFINE(spi_mutex);

/* Wait for EMW3080B to be ready for communication */
static int emw3080_spi_wait_ready(const struct device *spi_dev, uint32_t timeout_ms)
{
    uint8_t status_cmd = 0x04; /* Status command */
    uint8_t status = 0;
    uint32_t start_time = k_uptime_get_32();
    
    while ((k_uptime_get_32() - start_time) < timeout_ms) {
        int ret = emw3080_spi_transceive(spi_dev, &status_cmd, 1, &status, 1);
        if (ret == 0 && (status & 0x01) == 0) {
            /* Device is ready (bit 0 clear) */
            return 0;
        }
        k_msleep(1);
    }
    
    LOG_WRN("EMW3080B not ready after %u ms (status: 0x%02x)", timeout_ms, status);
    return -ETIMEDOUT;
}

/* Low-level SPI transceive with proper CS handling */
int emw3080_spi_transceive(const struct device *spi_dev, 
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len)
{
    if (!spi_dev || !device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    struct spi_buf tx_bufs[] = {
        {.buf = (void *)tx_buf, .len = tx_len}
    };
    struct spi_buf rx_bufs[] = {
        {.buf = rx_buf, .len = rx_len}
    };
    
    struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 1};
    struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = 1};
    
    int ret = spi_transceive(spi_dev, &emw3080_spi_cfg, &tx_set, &rx_set);
    if (ret < 0) {
        LOG_ERR("SPI transceive failed: %d", ret);
    }
    
    return ret;
}

/* Send data frame to EMW3080B */
int emw3080_spi_send_frame(const struct device *spi_dev,
                          const uint8_t *data, size_t data_len)
{
    if (!spi_dev || !data) {
        return -EINVAL;
    }
    
    if (data_len > EMW3080_SPI_MAX_PAYLOAD_SIZE) {
        LOG_ERR("Data too large: %zu > %d", data_len, EMW3080_SPI_MAX_PAYLOAD_SIZE);
        return -EINVAL;
    }
    
    /* Acquire mutex for thread-safe access */
    int ret = k_mutex_lock(&spi_mutex, K_MSEC(1000));
    if (ret != 0) {
        LOG_ERR("Failed to acquire SPI mutex");
        return ret;
    }
    
    /* Wait for device to be ready */
    ret = emw3080_spi_wait_ready(spi_dev, 100);
    if (ret != 0) {
        k_mutex_unlock(&spi_mutex);
        return ret;
    }
    
    /* Prepare frame */
    uint8_t frame[EMW3080_SPI_MAX_FRAME_SIZE];
    struct emw3080_spi_header *header = (struct emw3080_spi_header *)frame;
    
    header->magic = EMW3080_SPI_MAGIC_WRITE;
    header->reserved = 0;
    header->length = sys_cpu_to_le16(data_len);
    
    /* Copy payload */
    memcpy(frame + EMW3080_SPI_HEADER_SIZE, data, data_len);
    
    size_t frame_len = EMW3080_SPI_HEADER_SIZE + data_len;
    
    LOG_DBG("Sending SPI frame: magic=0x%02x, len=%u, total=%zu", 
            header->magic, data_len, frame_len);
    
    /* Send frame */
    ret = emw3080_spi_transceive(spi_dev, frame, frame_len, NULL, 0);
    
    k_mutex_unlock(&spi_mutex);
    return ret;
}

/* Receive data frame from EMW3080B */
int emw3080_spi_recv_frame(const struct device *spi_dev,
                          uint8_t *data, size_t max_len, size_t *received_len)
{
    if (!spi_dev || !data || !received_len) {
        return -EINVAL;
    }
    
    *received_len = 0;
    
    /* Acquire mutex for thread-safe access */
    int ret = k_mutex_lock(&spi_mutex, K_MSEC(1000));
    if (ret != 0) {
        LOG_ERR("Failed to acquire SPI mutex");
        return ret;
    }
    
    /* Check if data is available */
    uint8_t status_cmd = 0x04;
    uint8_t status = 0;
    
    ret = emw3080_spi_transceive(spi_dev, &status_cmd, 1, &status, 1);
    if (ret != 0) {
        k_mutex_unlock(&spi_mutex);
        return ret;
    }
    
    if ((status & 0x02) == 0) {
        /* No data available (bit 1 clear) */
        k_mutex_unlock(&spi_mutex);
        return -ENODATA;
    }
    
    /* Read frame header */
    uint8_t read_cmd = EMW3080_SPI_MAGIC_READ;
    uint8_t header_buf[EMW3080_SPI_HEADER_SIZE];
    
    ret = emw3080_spi_transceive(spi_dev, &read_cmd, 1, header_buf, EMW3080_SPI_HEADER_SIZE);
    if (ret != 0) {
        k_mutex_unlock(&spi_mutex);
        return ret;
    }
    
    struct emw3080_spi_header *header = (struct emw3080_spi_header *)header_buf;
    uint16_t payload_len = sys_le16_to_cpu(header->length);
    
    if (payload_len > max_len) {
        LOG_ERR("Received frame too large: %u > %zu", payload_len, max_len);
        k_mutex_unlock(&spi_mutex);
        return -ENOMEM;
    }
    
    if (payload_len > 0) {
        /* Read payload */
        ret = emw3080_spi_transceive(spi_dev, NULL, 0, data, payload_len);
        if (ret == 0) {
            *received_len = payload_len;
            LOG_DBG("Received SPI frame: len=%u", payload_len);
        }
    }
    
    k_mutex_unlock(&spi_mutex);
    return ret;
}

/* Send AT command via SPI using EMW3080B protocol */
int emw3080_spi_send_at_cmd(const struct device *spi_dev,
                           const char *cmd, size_t cmd_len,
                           char *resp_buf, size_t resp_len,
                           uint32_t timeout_ms)
{
    if (!spi_dev || !cmd || !resp_buf) {
        return -EINVAL;
    }
    
    LOG_DBG("Sending AT command: %.*s", (int)cmd_len, cmd);
    
    /* Send command using frame protocol */
    int ret = emw3080_spi_send_frame(spi_dev, (const uint8_t *)cmd, cmd_len);
    if (ret != 0) {
        LOG_ERR("Failed to send AT command frame: %d", ret);
        return ret;
    }
    
    /* Wait for response with timeout */
    uint32_t start_time = k_uptime_get_32();
    
    while ((k_uptime_get_32() - start_time) < timeout_ms) {
        size_t received_len;
        ret = emw3080_spi_recv_frame(spi_dev, (uint8_t *)resp_buf, resp_len - 1, &received_len);
        
        if (ret == 0 && received_len > 0) {
            /* Null-terminate response */
            resp_buf[received_len] = '\0';
            LOG_DBG("Received AT response: %.*s", (int)received_len, resp_buf);
            return 0;
        } else if (ret != -ENODATA) {
            /* Real error, not just no data available */
            LOG_ERR("Failed to receive AT response: %d", ret);
            return ret;
        }
        
        /* Wait a bit before retrying */
        k_msleep(10);
    }
    
    LOG_ERR("AT command timeout after %u ms", timeout_ms);
    return -ETIMEDOUT;
}

/* Initialize SPI communication with EMW3080B */
int emw3080_spi_init(const struct device *spi_dev)
{
    if (!spi_dev) {
        LOG_ERR("No SPI device provided");
        return -EINVAL;
    }
    
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }
    
    LOG_INF("Initializing EMW3080B SPI communication at %u Hz on device: %s", 
            emw3080_spi_cfg.frequency, spi_dev->name);
    
    /* Initialize mutex */
    k_mutex_init(&spi_mutex);
    
    /* Test communication with simple AT command */
    char response[128];
    const char *test_cmd = "AT\r\n";
    int ret = emw3080_spi_send_at_cmd(spi_dev, test_cmd, strlen(test_cmd), 
                                     response, sizeof(response), 5000);
    if (ret == 0) {
        LOG_INF("EMW3080B SPI communication initialized successfully");
        return 0;
    } else {
        LOG_WRN("EMW3080B SPI test command failed (%d), but continuing", ret);
        /* Continue anyway as the device might not be fully ready yet */
        return 0;
    }
}
