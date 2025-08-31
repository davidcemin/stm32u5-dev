/*
 * Simple EMW3080 SPI test - original working approach
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <string.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* Get SPI device from devicetree */
#define SPI_DEV_NODE DT_NODELABEL(spi2)
static const struct device *spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);

/* Simple SPI configuration based on original working code */
static struct spi_config spi_cfg = {
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPOL | SPI_MODE_CPHA,
    .frequency = 1000000,  /* 1 MHz */
    .slave = 0,
};

/* Simple SPI initialization - NO GPIO RESET as per original working code */
static int simple_spi_init(void)
{
    LOG_INF("=== Simple SPI Initialization (Original Approach) ===");
    
    /* Check if SPI device is ready */
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("SPI device not ready");
        return -1;
    }
    
    LOG_INF("✅ SPI device ready: %s", spi_dev->name);
    
    /* No GPIO reset - original working code never did this! */
    LOG_INF("ℹ️  Skipping GPIO reset (original working approach)");
    
    return 0;
}

/* Simple SPI transceive function */
static int simple_spi_transceive(const uint8_t *tx_data, size_t tx_len, 
                                  uint8_t *rx_data, size_t rx_len)
{
    struct spi_buf tx_buf = {
        .buf = (void *)tx_data,
        .len = tx_len
    };
    
    struct spi_buf rx_buf = {
        .buf = rx_data,
        .len = rx_len
    };
    
    struct spi_buf_set tx_bufs = {
        .buffers = &tx_buf,
        .count = 1
    };
    
    struct spi_buf_set rx_bufs = {
        .buffers = &rx_buf,
        .count = 1
    };
    
    return spi_transceive(spi_dev, &spi_cfg, &tx_bufs, &rx_bufs);
}

/* Simple MAC address request test */
static int simple_mac_test(void)
{
    LOG_INF("=== Simple MAC Address Test ===");
    
    /* Simple MAC request command - based on MXCH protocol */
    uint8_t mac_cmd[] = {
        0x4D, 0x58, 0x43, 0x48,  /* MXCH sync pattern */
        0x00, 0x00, 0x00, 0x10,  /* Length = 16 bytes */
        0x00, 0x02,              /* Type = WiFi GET MAC */
        0x00, 0x00,              /* Flags */
        0x00, 0x00, 0x00, 0x00   /* Payload (empty for GET MAC) */
    };
    
    uint8_t rx_buffer[64];
    memset(rx_buffer, 0, sizeof(rx_buffer));
    
    LOG_INF("Sending MAC request command...");
    LOG_HEXDUMP_INF(mac_cmd, sizeof(mac_cmd), "TX:");
    
    int ret = simple_spi_transceive(mac_cmd, sizeof(mac_cmd), rx_buffer, sizeof(rx_buffer));
    if (ret != 0) {
        LOG_ERR("SPI transceive failed: %d", ret);
        return ret;
    }
    
    LOG_INF("Received response:");
    LOG_HEXDUMP_INF(rx_buffer, 32, "RX:");
    
    /* Check if we got non-zero response */
    bool all_zero = true;
    for (int i = 0; i < 32; i++) {
        if (rx_buffer[i] != 0) {
            all_zero = false;
            break;
        }
    }
    
    if (all_zero) {
        LOG_ERR("❌ Received all zeros - module not responding");
        return -1;
    } else {
        LOG_INF("✅ Received non-zero response - module is responding!");
        
        /* Look for MAC address pattern in response */
        for (int i = 0; i < 26; i++) {
            if (rx_buffer[i] == 0x4D && rx_buffer[i+1] == 0x58 && 
                rx_buffer[i+2] == 0x43 && rx_buffer[i+3] == 0x48) {
                LOG_INF("Found MXCH response at offset %d", i);
                if (i + 16 < sizeof(rx_buffer)) {
                    LOG_INF("MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", 
                           rx_buffer[i+10], rx_buffer[i+11], rx_buffer[i+12],
                           rx_buffer[i+13], rx_buffer[i+14], rx_buffer[i+15]);
                }
                break;
            }
        }
    }
    
    return 0;
}

int main(void)
{
    LOG_INF("Starting Simple EMW3080 Test (Original Working Approach)");
    
    /* Wait a moment for system to stabilize */
    k_sleep(K_MSEC(1000));
    
    /* Initialize SPI (no reset) */
    int ret = simple_spi_init();
    if (ret != 0) {
        LOG_ERR("SPI initialization failed");
        return ret;
    }
    
    /* Wait a moment */
    k_sleep(K_MSEC(500));
    
    /* Test MAC address request */
    ret = simple_mac_test();
    if (ret != 0) {
        LOG_ERR("MAC test failed");
    }
    
    LOG_INF("Test complete. System will continue running...");
    
    /* Keep system running */
    while (1) {
        k_sleep(K_SECONDS(10));
        LOG_INF("System still running...");
    }
    
    return 0;
}
