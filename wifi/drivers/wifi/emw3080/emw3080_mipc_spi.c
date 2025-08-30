/**
 * @file emw3080_mipc_spi.c
 * @brief MIPC-SPI integration for EMW3080
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include "emw3080_mipc.h"
#include "../drivers/wifi/emw3080/emw3080_spi.h"

LOG_MODULE_REGISTER(emw3080_mipc_spi, LOG_LEVEL_DBG);

/* Global SPI device reference */
static const struct device *g_spi_device = NULL;

/* Buffer for receiving SPI data */
#define RX_BUFFER_SIZE 2048
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static struct k_work_q mipc_work_q;
static K_THREAD_STACK_DEFINE(mipc_work_stack, 1024);
static struct k_work rx_work;

/* SPI receive work handler */
static void mipc_rx_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    
    if (g_spi_device == NULL) {
        return;
    }

    /* Check if data is available */
    uint8_t status_cmd = EMW3080_SPI_STATUS_CMD;
    uint8_t status = 0;
    
    int ret = emw3080_spi_transceive(g_spi_device, &status_cmd, 1, &status, 1);
    if (ret != 0) {
        LOG_ERR("Failed to read status register: %d", ret);
        return;
    }

    /* Check if data is available */
    if (status & EMW3080_SPI_STATUS_DATA_AVAILABLE) {
        LOG_DBG("Data available, attempting to receive");
        
        size_t received_len = 0;
        ret = emw3080_spi_recv_frame(g_spi_device, rx_buffer, RX_BUFFER_SIZE, &received_len);
        if (ret == 0 && received_len > 0) {
            LOG_DBG("Received %zu bytes via SPI", received_len);
            LOG_HEXDUMP_DBG(rx_buffer, received_len, "MIPC RX:");
            
            /* Process the received data with MIPC */
            mipc_process_received_data(rx_buffer, received_len);
        } else {
            LOG_WRN("Failed to receive data or no data: ret=%d, len=%zu", ret, received_len);
        }
    }
}

/* MIPC send function - called by MIPC layer to send data via SPI */
static int mipc_spi_send(uint8_t *data, uint16_t size)
{
    if (g_spi_device == NULL) {
        LOG_ERR("SPI device not initialized");
        return -1;
    }

    if (data == NULL || size == 0) {
        LOG_ERR("Invalid send parameters");
        return -1;
    }

    LOG_DBG("Sending %u bytes via SPI", size);
    LOG_HEXDUMP_DBG(data, size, "MIPC TX:");

    /* Send the data using SPI frame protocol */
    int ret = emw3080_spi_send_frame(g_spi_device, data, size);
    if (ret != 0) {
        LOG_ERR("Failed to send MIPC data via SPI: %d", ret);
        return ret;
    }

    /* Schedule work to check for response */
    k_work_submit_to_queue(&mipc_work_q, &rx_work);

    return 0;
}

/* Initialize MIPC-SPI integration */
int emw3080_mipc_spi_init(void)
{
    /* Get SPI device */
    g_spi_device = device_get_binding("spi@40003800");
    if (!g_spi_device) {
        LOG_ERR("Cannot find SPI device");
        return -ENODEV;
    }

    if (!device_is_ready(g_spi_device)) {
        LOG_ERR("SPI device not ready");
        return -ENODEV;
    }

    /* Initialize SPI communication */
    int ret = emw3080_spi_init(g_spi_device);
    if (ret != 0) {
        LOG_ERR("Failed to initialize SPI: %d", ret);
        return ret;
    }

    /* Initialize work queue for RX processing */
    k_work_queue_init(&mipc_work_q);
    k_work_queue_start(&mipc_work_q, mipc_work_stack,
                       K_THREAD_STACK_SIZEOF(mipc_work_stack),
                       K_PRIO_COOP(7), NULL);
    
    k_work_init(&rx_work, mipc_rx_work_handler);

    /* Initialize MIPC protocol with our SPI send function */
    ret = mipc_init(mipc_spi_send);
    if (ret != MIPC_CODE_SUCCESS) {
        LOG_ERR("Failed to initialize MIPC: %d", ret);
        return -EINVAL;
    }

    LOG_INF("MIPC-SPI integration initialized successfully");
    return 0;
}

/* Deinitialize MIPC-SPI integration */
int emw3080_mipc_spi_deinit(void)
{
    mipc_deinit();
    g_spi_device = NULL;
    LOG_INF("MIPC-SPI integration deinitialized");
    return 0;
}

/* Poll for MIPC responses - should be called regularly */
void emw3080_mipc_spi_poll(void)
{
    /* Submit work to check for incoming data */
    k_work_submit_to_queue(&mipc_work_q, &rx_work);
}
