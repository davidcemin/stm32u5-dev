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
#include "emw3080_slip.h"

/* SPI configuration for EMW3080B */
static struct spi_config emw3080_spi_cfg = {
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
    .frequency = 8000000, /* 8 MHz - EMW3080B supports up to 20MHz */
    .slave = 0,
};

/* Mutex for SPI access synchronization */
static K_MUTEX_DEFINE(spi_mutex);

/* Wait for EMW3080B to be ready for communication */
int emw3080_spi_wait_ready(const struct device *spi_dev, uint32_t timeout_ms)
{
    uint8_t status_cmd = EMW3080_SPI_STATUS_CMD;
    uint8_t status = 0;
    uint32_t start_time = k_uptime_get_32();
    
    LOG_DBG("Waiting for EMW3080B ready status...");
    
    while ((k_uptime_get_32() - start_time) < timeout_ms) {
        int ret = emw3080_spi_transceive(spi_dev, &status_cmd, 1, &status, 1);
        if (ret == 0) {
            LOG_DBG("Status register: 0x%02x (ready=%s, data_avail=%s, busy=%s)", 
                   status,
                   (status & EMW3080_SPI_STATUS_READY) ? "no" : "yes",
                   (status & EMW3080_SPI_STATUS_DATA_AVAILABLE) ? "yes" : "no", 
                   (status & EMW3080_SPI_STATUS_BUSY) ? "yes" : "no");
            
            /* Device is ready when ready bit is clear and not busy */
            if ((status & EMW3080_SPI_STATUS_READY) == 0 && 
                (status & EMW3080_SPI_STATUS_BUSY) == 0) {
                LOG_DBG("EMW3080B is ready");
                return 0;
            }
        } else {
            LOG_DBG("Status read failed: %d", ret);
        }
        k_msleep(1);
    }
    
    LOG_WRN("EMW3080B not ready after %u ms (final status: 0x%02x)", timeout_ms, status);
    return -ETIMEDOUT;
}

/* Check if data is available for reading */
static bool emw3080_spi_data_available(const struct device *spi_dev)
{
    uint8_t status_cmd = EMW3080_SPI_STATUS_CMD;
    uint8_t status = 0;
    
    int ret = emw3080_spi_transceive(spi_dev, &status_cmd, 1, &status, 1);
    if (ret == 0) {
        bool data_avail = (status & EMW3080_SPI_STATUS_DATA_AVAILABLE) != 0;
        bool ready = (status & EMW3080_SPI_STATUS_READY) == 0; /* 0 = ready */
        bool busy = (status & EMW3080_SPI_STATUS_BUSY) != 0;
        
        LOG_INF("EMW3080 Status: 0x%02x - Ready:%s, DataAvail:%s, Busy:%s", 
               status, ready ? "YES" : "NO", data_avail ? "YES" : "NO", busy ? "YES" : "NO");
        return data_avail;
    }
    
    LOG_ERR("Status check failed: %d", ret);
    return false;
}

/* Low-level SPI transceive with proper CS handling */
int emw3080_spi_transceive(const struct device *spi_dev, 
                          const uint8_t *tx_buf, size_t tx_len,
                          uint8_t *rx_buf, size_t rx_len)
{
    /* CRITICAL: Add extensive safety checks to prevent crashes */
    LOG_INF("EMW3080 SPI: Starting transceive - tx_len=%zu, rx_len=%zu", tx_len, rx_len);
    
    if (!spi_dev) {
        LOG_ERR("EMW3080 SPI: NULL SPI device pointer");
        return -ENODEV;
    }
    
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("EMW3080 SPI: Device %s not ready", spi_dev->name);
        return -ENODEV;
    }
    
    /* Validate the SPI device API before using it */
    if (!spi_dev->api) {
        LOG_ERR("EMW3080 SPI: Device has no API");
        return -ENODEV;
    }
    
    const struct spi_driver_api *api = (const struct spi_driver_api *)spi_dev->api;
    if (!api->transceive) {
        LOG_ERR("EMW3080 SPI: Device API has no transceive function");
        return -ENODEV;
    }
    
    /* Validate buffer parameters */
    if (tx_len > 0 && !tx_buf) {
        LOG_ERR("EMW3080 SPI: TX buffer NULL but length > 0");
        return -EINVAL;
    }
    
    if (rx_len > 0 && !rx_buf) {
        LOG_ERR("EMW3080 SPI: RX buffer NULL but length > 0");
        return -EINVAL;
    }
    
    /* Validate reasonable buffer sizes to prevent memory issues */
    if (tx_len > 1024 || rx_len > 1024) {
        LOG_ERR("EMW3080 SPI: Buffer size too large (tx:%zu, rx:%zu)", tx_len, rx_len);
        return -EINVAL;
    }
    
    LOG_INF("EMW3080 SPI: Safety checks passed, attempting SPI transaction");
    
    /* SAFE APPROACH: Try the transaction with error handling */
    struct spi_buf tx_bufs[] = {
        {.buf = (void *)tx_buf, .len = tx_len}
    };
    struct spi_buf rx_bufs[] = {
        {.buf = rx_buf, .len = rx_len}
    };
    
    struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = (tx_len > 0) ? 1 : 0};
    struct spi_buf_set rx_set = {.buffers = rx_bufs, .count = (rx_len > 0) ? 1 : 0};
    
    /* Log what we're about to do */
    LOG_INF("EMW3080 SPI: Calling spi_transceive with config freq=%d", emw3080_spi_cfg.frequency);
    
    /* CRITICAL: Try to detect SPI configuration issues before they crash */
    if (emw3080_spi_cfg.frequency == 0) {
        LOG_ERR("EMW3080 SPI: Invalid SPI frequency (0)");
        return -EINVAL;
    }
    
    /* Attempt the actual SPI transaction */
    int ret = spi_transceive(spi_dev, &emw3080_spi_cfg, &tx_set, &rx_set);
    
    if (ret < 0) {
        LOG_ERR("EMW3080 SPI: Transaction failed with error %d", ret);
        
        /* Log more details about the failure */
        LOG_ERR("EMW3080 SPI: Device: %s", spi_dev->name);
        LOG_ERR("EMW3080 SPI: Config - freq:%d, op:0x%08x", 
                emw3080_spi_cfg.frequency, emw3080_spi_cfg.operation);
        
        return ret;
    }
    
    LOG_INF("EMW3080 SPI: Transaction completed successfully");
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
    
    /* Prepare MX WiFi compatible frame */
    uint8_t frame[EMW3080_SPI_MAX_FRAME_SIZE];
    struct emw3080_spi_header *header = (struct emw3080_spi_header *)frame;
    
    /* Set MX WiFi header format */
    header->type = EMW3080_SPI_WRITE;
    header->len = sys_cpu_to_le16((uint16_t)data_len);
    header->lenx = sys_cpu_to_le16((uint16_t)(data_len ^ 0xFFFF));
    memset(header->dummy, 0, sizeof(header->dummy));
    
    /* Copy payload */
    memcpy(frame + EMW3080_SPI_HEADER_SIZE, data, data_len);
    
    size_t frame_len = EMW3080_SPI_HEADER_SIZE + data_len;
    
    LOG_DBG("Sending MX WiFi SPI frame: type=0x%02x, len=%u, lenx=0x%04x, total=%zu", 
            header->type, sys_le16_to_cpu(header->len), sys_le16_to_cpu(header->lenx), frame_len);
    
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
    bool data_available = emw3080_spi_data_available(spi_dev);
    LOG_INF("EMW3080 recv_frame: Data available = %s", data_available ? "YES" : "NO");
    
    if (!data_available) {
        k_mutex_unlock(&spi_mutex);
        return -ENODATA;
    }
    
    /* Wait a moment for device to be ready for read operation */
    ret = emw3080_spi_wait_ready(spi_dev, 50);
    if (ret != 0) {
        LOG_DBG("Device not ready for read, but data available - continuing anyway");
    }
    
    /* Read frame header first */
    uint8_t read_cmd = EMW3080_SPI_READ;
    uint8_t header_buf[EMW3080_SPI_HEADER_SIZE];
    
    LOG_DBG("Reading frame header...");
    ret = emw3080_spi_transceive(spi_dev, &read_cmd, 1, header_buf, EMW3080_SPI_HEADER_SIZE);
    if (ret != 0) {
        LOG_ERR("Failed to read frame header: %d", ret);
        k_mutex_unlock(&spi_mutex);
        return ret;
    }
    
    struct emw3080_spi_header *header = (struct emw3080_spi_header *)header_buf;
    uint16_t payload_len = sys_le16_to_cpu(header->len);
    uint16_t payload_lenx = sys_le16_to_cpu(header->lenx);
    
    LOG_DBG("Frame header: type=0x%02x, len=%u, lenx=0x%04x", 
           header->type, payload_len, payload_lenx);
    
    /* Validate length field (MX WiFi protocol) */
    if ((payload_len ^ payload_lenx) != 0xFFFF) {
        LOG_ERR("Invalid length validation: %04x ^ %04x != 0xFFFF", payload_len, payload_lenx);
        k_mutex_unlock(&spi_mutex);
        return -EBADMSG;
    }
    
    if (payload_len > max_len) {
        LOG_ERR("Received frame too large: %u > %zu", payload_len, max_len);
        k_mutex_unlock(&spi_mutex);
        return -ENOMEM;
    }
    
    if (payload_len > 0) {
        /* Read payload data */
        LOG_DBG("Reading %u bytes of payload...", payload_len);
        ret = emw3080_spi_transceive(spi_dev, NULL, 0, data, payload_len);
        if (ret == 0) {
            *received_len = payload_len;
            LOG_DBG("Successfully received frame: %u bytes", payload_len);
            
            /* Debug: print first few bytes of payload */
            if (payload_len > 0) {
                LOG_DBG("Payload preview: %02x %02x %02x %02x...", 
                       data[0], 
                       payload_len > 1 ? data[1] : 0,
                       payload_len > 2 ? data[2] : 0,
                       payload_len > 3 ? data[3] : 0);
            }
        } else {
            LOG_ERR("Failed to read payload: %d", ret);
        }
    } else {
        LOG_DBG("Empty frame (no payload)");
    }
    
    k_mutex_unlock(&spi_mutex);
    return ret;
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
    
    /* Wait for device to be ready */
    LOG_INF("Waiting for EMW3080B to be ready...");
    int ret = emw3080_spi_wait_ready(spi_dev, 2000);
    if (ret != 0) {
        LOG_WRN("Device not ready, but continuing initialization");
    }
    
    /* Test basic SPI hardware communication */
    LOG_INF("Testing basic SPI hardware...");
    uint8_t status_cmd = EMW3080_SPI_STATUS_CMD;
    uint8_t status = 0;
    
    ret = emw3080_spi_transceive(spi_dev, &status_cmd, 1, &status, 1);
    if (ret == 0) {
        LOG_INF("EMW3080B SPI hardware communication working");
        LOG_DBG("Status register: 0x%02x", status);
    } else {
        LOG_WRN("SPI hardware test failed (%d), but continuing", ret);
    }
    
    LOG_INF("EMW3080B SPI interface initialized for MIPC protocol");
    return 0;
}

/* ================================== */
/* SLIP-Enhanced SPI Functions */
/* ================================== */

int emw3080_spi_send_slip_frame(const struct device *spi_dev,
                                const uint8_t *data, size_t data_len)
{
    if (!spi_dev || !data || data_len == 0) {
        return -EINVAL;
    }
    
    /* Encode data with SLIP framing */
    uint8_t slip_buffer[emw3080_slip_max_encoded_size(data_len)];
    uint16_t encoded_len;
    
    int ret = emw3080_slip_encode(data, data_len, slip_buffer, sizeof(slip_buffer), &encoded_len);
    if (ret != 0) {
        LOG_ERR("SLIP encoding failed: %d", ret);
        return ret;
    }
    
    LOG_DBG("Sending SLIP frame: %d bytes encoded to %d bytes", data_len, encoded_len);
    
    /* Send SLIP-encoded frame via standard SPI */
    return emw3080_spi_send_frame(spi_dev, slip_buffer, encoded_len);
}

int emw3080_spi_recv_slip_frame(const struct device *spi_dev,
                                uint8_t *data, size_t max_len, size_t *received_len)
{
    if (!spi_dev || !data || !received_len) {
        return -EINVAL;
    }
    
    /* Receive raw SPI frame */
    uint8_t slip_buffer[EMW3080_SPI_MAX_FRAME_SIZE];
    size_t slip_len;
    
    int ret = emw3080_spi_recv_frame(spi_dev, slip_buffer, sizeof(slip_buffer), &slip_len);
    if (ret != 0) {
        return ret;
    }
    
    if (slip_len == 0) {
        *received_len = 0;
        return 0;
    }
    
    /* Decode SLIP frame */
    uint16_t decoded_len;
    ret = emw3080_slip_decode(slip_buffer, slip_len, data, max_len, &decoded_len);
    if (ret != 0) {
        if (ret == -EAGAIN) {
            /* No complete frame yet */
            *received_len = 0;
            return 0;
        }
        LOG_ERR("SLIP decoding failed: %d", ret);
        return ret;
    }
    
    *received_len = decoded_len;
    LOG_DBG("Received SLIP frame: %d bytes decoded from %d bytes", decoded_len, slip_len);
    
    return 0;
}
