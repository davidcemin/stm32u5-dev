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
#include "emw3080.h"

/* SPI configuration for EMW3080B */
static struct spi_config emw3080_spi_cfg = {
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
    .frequency = 1000000, /* 1 MHz initially */
    .slave = 0,
};

/* Basic SPI communication functions */
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
    
    return spi_transceive(spi_dev, &emw3080_spi_cfg, &tx_set, &rx_set);
}

/* Send AT command over SPI (simplified implementation) */
int emw3080_spi_send_at_cmd(const struct device *spi_dev,
                           const char *cmd, size_t cmd_len,
                           char *resp_buf, size_t resp_len,
                           uint32_t timeout_ms)
{
    int ret;
    uint8_t dummy_tx[256];
    uint8_t rx_data[256];
    
    if (!spi_dev || !cmd || !resp_buf) {
        return -EINVAL;
    }
    
    if (cmd_len > sizeof(dummy_tx)) {
        cmd_len = sizeof(dummy_tx);
    }
    
    /* For now, we'll try a simple approach: */
    /* 1. Send the command data */
    memcpy(dummy_tx, cmd, cmd_len);
    
    LOG_INF("Sending AT command over SPI: %.*s", (int)cmd_len, cmd);
    
    ret = emw3080_spi_transceive(spi_dev, dummy_tx, cmd_len, rx_data, sizeof(rx_data));
    if (ret < 0) {
        LOG_ERR("SPI transceive failed: %d", ret);
        return ret;
    }
    
    /* Copy response data */
    size_t copy_len = MIN(resp_len - 1, sizeof(rx_data));
    memcpy(resp_buf, rx_data, copy_len);
    resp_buf[copy_len] = '\0';
    
    LOG_INF("SPI response: %.*s", (int)copy_len, resp_buf);
    
    return 0;
}

/* Initialize SPI communication */
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
    
    LOG_INF("EMW3080 SPI communication initialized on device: %s", spi_dev->name);
    return 0;
}
