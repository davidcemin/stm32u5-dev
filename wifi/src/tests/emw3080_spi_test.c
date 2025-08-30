/*
 * EMW3080 SPI Basic Test Functions
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include "../../drivers/wifi/emw3080/emw3080_spi.h"

LOG_MODULE_REGISTER(emw3080_spi_test, CONFIG_LOG_DEFAULT_LEVEL);

/* Get SPI device from devicetree */
#define SPI_DEV_NODE DT_NODELABEL(spi2)
static const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);

/* Basic SPI initialization function */
int emw3080_spi_init_basic(void)
{
    LOG_INF("=== EMW3080 SPI Basic Initialization ===");
    
    /* Check if SPI device is ready */
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -1;
    }
    
    /* Initialize EMW3080 SPI driver */
    int ret = emw3080_spi_init(spi_dev);
    if (ret != 0) {
        LOG_ERR("EMW3080 SPI initialization failed: %d", ret);
        return ret;
    }
    
    LOG_INF("✅ SPI device and EMW3080 SPI initialized successfully");
    return 0;
}

int emw3080_spi_basic_test(void)
{
    LOG_INF("=== EMW3080 SPI Basic Test ===");
    
    /* Verify SPI device is still ready */
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -1;
    }
    
    LOG_INF("✅ SPI device ready: %s", spi_dev->name);
    
    /* Test 1: Basic SPI transaction */
    uint8_t tx_data = 0x55;  /* Test pattern */
    uint8_t rx_data = 0x00;
    
    int ret = emw3080_spi_transceive(spi_dev, &tx_data, 1, &rx_data, 1);
    if (ret != 0) {
        LOG_ERR("SPI transaction failed: %d", ret);
        return ret;
    }
    
    LOG_INF("✅ SPI transaction completed - sent: 0x%02x, received: 0x%02x", 
            tx_data, rx_data);
    
    /* Test 2: Multi-byte transaction */
    uint8_t multi_tx[] = {0xAA, 0x55, 0xCC, 0x33};
    uint8_t multi_rx[4] = {0};
    
    ret = emw3080_spi_transceive(spi_dev, multi_tx, sizeof(multi_tx), multi_rx, sizeof(multi_rx));
    if (ret != 0) {
        LOG_ERR("Multi-byte SPI transaction failed: %d", ret);
        return ret;
    }
    
    LOG_INF("✅ Multi-byte SPI test completed");
    LOG_INF("   Sent: %02x %02x %02x %02x", 
            multi_tx[0], multi_tx[1], multi_tx[2], multi_tx[3]);
    LOG_INF("   Recv: %02x %02x %02x %02x", 
            multi_rx[0], multi_rx[1], multi_rx[2], multi_rx[3]);
    
    LOG_INF("🎉 EMW3080 SPI basic test PASSED");
    return 0;
}
