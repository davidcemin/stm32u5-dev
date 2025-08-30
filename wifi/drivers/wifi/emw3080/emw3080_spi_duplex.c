/**
 * @file emw3080_spi_duplex.c
 * @brief Full-duplex SPI implementation based on ST's MX WiFi library
 * 
 * This implements the proper full-duplex SPI pattern that the EMW3080 expects:
 * 1. Send TX header while receiving RX header simultaneously
 * 2. Validate received header
 * 3. Send TX data while receiving RX data simultaneously
 */

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include "emw3080_spi.h"

LOG_MODULE_DECLARE(emw3080_spi, CONFIG_WIFI_LOG_LEVEL);

/* External access to SPI transceive function */
extern int emw3080_spi_transceive(const struct device *spi_dev,
                                 const uint8_t *tx_buf, size_t tx_len,
                                 uint8_t *rx_buf, size_t rx_len);

extern int emw3080_spi_wait_ready(const struct device *spi_dev, uint32_t timeout_ms);

/* Full-duplex SPI transaction following ST's pattern */
int emw3080_spi_full_duplex_transaction(const struct device *spi_dev,
                                       const uint8_t *tx_data, size_t tx_len,
                                       uint8_t *rx_data, size_t rx_max_len, size_t *rx_len)
{
    if (!spi_dev || !rx_len) {
        return -EINVAL;
    }
    
    *rx_len = 0;
    
    /* Wait for device to be ready */
    int ret = emw3080_spi_wait_ready(spi_dev, 100);
    if (ret != 0) {
        LOG_ERR("Device not ready for full-duplex transaction");
        return ret;
    }
    
    /* Prepare TX header */
    struct emw3080_spi_header tx_header = {0};
    struct emw3080_spi_header rx_header = {0};
    
    if (tx_data && tx_len > 0) {
        tx_header.type = EMW3080_SPI_WRITE;
        tx_header.len = sys_cpu_to_le16((uint16_t)tx_len);
        tx_header.lenx = sys_cpu_to_le16((uint16_t)(tx_len ^ 0xFFFF));
    } else {
        /* Status query header */
        tx_header.type = EMW3080_SPI_STATUS_CMD;
        tx_header.len = 0;
        tx_header.lenx = 0xFFFF;
    }
    
    LOG_DBG("Full-duplex: TX header - type=0x%02x, len=%u, lenx=0x%04x", 
            tx_header.type, sys_le16_to_cpu(tx_header.len), sys_le16_to_cpu(tx_header.lenx));
    
    /* Step 1: Exchange headers simultaneously */
    ret = emw3080_spi_transceive(spi_dev, (uint8_t *)&tx_header, sizeof(tx_header),
                                (uint8_t *)&rx_header, sizeof(rx_header));
    if (ret != 0) {
        LOG_ERR("Failed to exchange headers: %d", ret);
        return ret;
    }
    
    /* Validate received header */
    uint16_t rx_data_len = sys_le16_to_cpu(rx_header.len);
    uint16_t rx_lenx = sys_le16_to_cpu(rx_header.lenx);
    
    LOG_DBG("Full-duplex: RX header - type=0x%02x, len=%u, lenx=0x%04x", 
            rx_header.type, rx_data_len, rx_lenx);
    
    if (rx_header.type != EMW3080_SPI_READ) {
        LOG_WRN("Invalid RX header type: 0x%02x (expected 0x%02x)", 
                rx_header.type, EMW3080_SPI_READ);
    }
    
    if ((rx_data_len ^ rx_lenx) != 0xFFFF) {
        LOG_WRN("Invalid RX header length validation: len=0x%04x, lenx=0x%04x", 
                rx_data_len, rx_lenx);
    }
    
    /* Step 2: Exchange data if either side has data */
    if ((tx_len > 0) || (rx_data_len > 0)) {
        /* Use the maximum length for the transaction */
        size_t transaction_len = (tx_len > rx_data_len) ? tx_len : rx_data_len;
        
        if (transaction_len > EMW3080_SPI_MAX_DATA_SIZE) {
            LOG_ERR("Transaction length too large: %zu", transaction_len);
            return -EMSGSIZE;
        }
        
        /* Prepare buffers */
        uint8_t tx_buffer[EMW3080_SPI_MAX_DATA_SIZE] = {0};
        uint8_t rx_buffer[EMW3080_SPI_MAX_DATA_SIZE] = {0};
        
        /* Copy TX data if available */
        if (tx_data && tx_len > 0) {
            memcpy(tx_buffer, tx_data, tx_len);
        }
        
        LOG_DBG("Full-duplex: Data exchange - tx_len=%zu, rx_len=%u, transaction_len=%zu", 
                tx_len, rx_data_len, transaction_len);
        
        /* Exchange data simultaneously */
        ret = emw3080_spi_transceive(spi_dev, tx_buffer, transaction_len,
                                    rx_buffer, transaction_len);
        if (ret != 0) {
            LOG_ERR("Failed to exchange data: %d", ret);
            return ret;
        }
        
        /* Copy received data */
        if (rx_data && rx_data_len > 0 && rx_max_len > 0) {
            size_t copy_len = (rx_data_len < rx_max_len) ? rx_data_len : rx_max_len;
            memcpy(rx_data, rx_buffer, copy_len);
            *rx_len = copy_len;
            
            LOG_DBG("Full-duplex: Received %zu bytes", copy_len);
        }
    }
    
    return 0;
}

/* Wrapper for sending data with full-duplex pattern */
int emw3080_spi_send_frame_duplex(const struct device *spi_dev,
                                 const uint8_t *data, size_t data_len)
{
    size_t rx_len = 0;
    return emw3080_spi_full_duplex_transaction(spi_dev, data, data_len, NULL, 0, &rx_len);
}

/* Wrapper for receiving data with full-duplex pattern */
int emw3080_spi_recv_frame_duplex(const struct device *spi_dev,
                                 uint8_t *data, size_t max_len, size_t *received_len)
{
    return emw3080_spi_full_duplex_transaction(spi_dev, NULL, 0, data, max_len, received_len);
}

/* Combined send and receive in single full-duplex transaction */
int emw3080_spi_send_recv_frame(const struct device *spi_dev,
                               const uint8_t *tx_data, size_t tx_len,
                               uint8_t *rx_data, size_t rx_max_len, size_t *rx_len)
{
    return emw3080_spi_full_duplex_transaction(spi_dev, tx_data, tx_len, rx_data, rx_max_len, rx_len);
}
