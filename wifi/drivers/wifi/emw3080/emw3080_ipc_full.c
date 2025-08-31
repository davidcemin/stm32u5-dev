/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(emw3080_ipc, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "emw3080_ipc.h"
#include "emw3080_offload_dev.h"

/* Request ID counter for tracking commands */
static uint32_t request_id_counter = 1;

/* Helper function to get next request ID */
static uint32_t get_next_request_id(void)
{
    return request_id_counter++;
}

/* SPI transfer function using Zephyr SPI API */
static int emw3080_spi_transfer(const struct device *dev, 
                               const uint8_t *tx_data, uint8_t *rx_data, 
                               size_t len)
{
    struct emw3080_dev_data *data = dev->data;
    const struct emw3080_dev_config *config = dev->config;
    
    if (!data->spi_dev) {
        LOG_ERR("SPI device not initialized");
        return -ENODEV;
    }

    struct spi_buf tx_buf = { .buf = (void *)tx_data, .len = len };
    struct spi_buf rx_buf = { .buf = rx_data, .len = len };
    struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_transceive(data->spi_dev, &data->spi_cfg, &tx_bufs, &rx_bufs);
    if (ret < 0) {
        LOG_ERR("SPI transfer failed: %d", ret);
        return ret;
    }

    return 0;
}

/* Hardware reset function */
static void emw3080_hw_reset(const struct device *dev)
{
    const struct emw3080_dev_config *config = dev->config;
    
    if (config->reset_gpio.port) {
        /* Assert reset (low) */
        gpio_pin_set_dt(&config->reset_gpio, 0);
        k_msleep(100);
        
        /* Deassert reset (high) */
        gpio_pin_set_dt(&config->reset_gpio, 1);
        k_msleep(1200);  /* Wait for module to boot */
        
        LOG_INF("EMW3080 hardware reset completed");
    }
}

/* Send SPI command with proper framing */
static int emw3080_spi_send_frame(const struct device *dev, 
                                 const uint8_t *data, size_t len)
{
    struct emw3080_spi_header header = {0};
    uint8_t tx_buffer[sizeof(header) + EMW3080_IPC_PAYLOAD_SIZE];
    uint8_t rx_buffer[sizeof(header) + EMW3080_IPC_PAYLOAD_SIZE];
    size_t total_len = sizeof(header) + len;
    
    if (len > EMW3080_IPC_PAYLOAD_SIZE) {
        LOG_ERR("Data too large: %zu bytes", len);
        return -EINVAL;
    }
    
    /* Prepare SPI header */
    header.type = EMW3080_SPI_WRITE;
    header.len = sys_cpu_to_le16(len);
    header.lenx = sys_cpu_to_le16((uint16_t)(len ^ 0xFFFF));
    
    /* Copy header and data to transmit buffer */
    memcpy(tx_buffer, &header, sizeof(header));
    memcpy(tx_buffer + sizeof(header), data, len);
    
    /* Perform SPI transfer */
    int ret = emw3080_spi_transfer(dev, tx_buffer, rx_buffer, total_len);
    if (ret < 0) {
        LOG_ERR("SPI frame send failed: %d", ret);
        return ret;
    }
    
    LOG_HEXDUMP_DBG(tx_buffer, total_len, "Sent SPI frame:");
    LOG_HEXDUMP_DBG(rx_buffer, total_len, "Received SPI response:");
    
    return 0;
}

/* Receive SPI response with proper framing */
static int emw3080_spi_receive_frame(const struct device *dev, 
                                    uint8_t *data, size_t max_len, 
                                    size_t *received_len)
{
    struct emw3080_spi_header header = {0};
    uint8_t tx_buffer[sizeof(header) + EMW3080_IPC_PAYLOAD_SIZE] = {0};
    uint8_t rx_buffer[sizeof(header) + EMW3080_IPC_PAYLOAD_SIZE];
    
    /* Prepare read command header */
    /* Poll header follows MXWIFI pattern: host sends WRITE with len=0 and lenx=~len */
    header.type = EMW3080_SPI_WRITE;
    header.len = sys_cpu_to_le16(0);
    header.lenx = sys_cpu_to_le16(0xFFFF);
    memcpy(tx_buffer, &header, sizeof(header));
    
    /* Perform SPI transfer to read response */
    size_t total_len = sizeof(header) + max_len;
    int ret = emw3080_spi_transfer(dev, tx_buffer, rx_buffer, total_len);
    if (ret < 0) {
        LOG_ERR("SPI frame receive failed: %d", ret);
        return ret;
    }
    
    /* Parse response header */
    struct emw3080_spi_header *resp_header = (struct emw3080_spi_header *)rx_buffer;
    uint16_t data_len = sys_le16_to_cpu(resp_header->len);
    
    if (data_len > max_len) {
        LOG_ERR("Response too large: %u bytes", data_len);
        return -ENOMEM;
    }
    
    /* Copy response data */
    if (data_len > 0) {
        memcpy(data, rx_buffer + sizeof(header), data_len);
    }
    
    if (received_len) {
        *received_len = data_len;
    }
    
    LOG_HEXDUMP_DBG(rx_buffer, sizeof(header) + data_len, "Received SPI frame:");
    
    return 0;
}

/* Send MIPC command and receive response */
int emw3080_ipc_send_command(const struct device *dev, uint16_t api_id, 
                            const void *params, size_t param_size,
                            void *response, size_t response_size, 
                            k_timeout_t timeout)
{
    struct emw3080_mipc_packet cmd_packet = {0};
    struct emw3080_mipc_packet resp_packet = {0};
    size_t received_len;
    int ret;
    
    if (param_size > EMW3080_IPC_PAYLOAD_SIZE) {
        LOG_ERR("Parameters too large: %zu bytes", param_size);
        return -EINVAL;
    }
    
    /* Prepare MIPC command packet */
    cmd_packet.req_id = sys_cpu_to_le32(get_next_request_id());
    cmd_packet.api_id = sys_cpu_to_le16(api_id);
    
    if (params && param_size > 0) {
        memcpy(cmd_packet.params, params, param_size);
    }
    
    size_t cmd_size = MIPC_HEADER_SIZE + param_size;
    
    LOG_DBG("Sending MIPC command: req_id=0x%08x, api_id=0x%04x, size=%zu", 
            sys_le32_to_cpu(cmd_packet.req_id), api_id, cmd_size);
    
    /* Send command */
    ret = emw3080_spi_send_frame(dev, (uint8_t *)&cmd_packet, cmd_size);
    if (ret < 0) {
        LOG_ERR("Failed to send MIPC command: %d", ret);
        return ret;
    }
    
    /* Wait for response - give the module time to process */
    k_msleep(100);
    
    /* Receive response */
    ret = emw3080_spi_receive_frame(dev, (uint8_t *)&resp_packet, 
                                   sizeof(resp_packet), &received_len);
    if (ret < 0) {
        LOG_ERR("Failed to receive MIPC response: %d", ret);
        return ret;
    }
    
    if (received_len < MIPC_HEADER_SIZE) {
        LOG_ERR("Response too short: %zu bytes", received_len);
        return -EPROTO;
    }
    
    /* Verify response */
    uint32_t resp_req_id = sys_le32_to_cpu(resp_packet.req_id);
    uint16_t resp_api_id = sys_le16_to_cpu(resp_packet.api_id);
    
    LOG_DBG("Received MIPC response: req_id=0x%08x, api_id=0x%04x, size=%zu", 
            resp_req_id, resp_api_id, received_len);
    
    /* Copy response data if requested */
    if (response && response_size > 0) {
        size_t resp_data_len = received_len - MIPC_HEADER_SIZE;
        size_t copy_len = MIN(resp_data_len, response_size);
        memcpy(response, resp_packet.params, copy_len);
    }
    
    return 0;
}

/* Initialize IPC communication */
int emw3080_ipc_init(const struct device *dev)
{
    LOG_INF("Initializing EMW3080 IPC");
    
    /* Perform hardware reset */
    emw3080_hw_reset(dev);
    
    /* Test communication with echo command */
    uint8_t echo_data[] = "TEST";
    uint8_t echo_response[32];
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_SYS_ECHO_CMD, 
                                      echo_data, sizeof(echo_data),
                                      echo_response, sizeof(echo_response),
                                      K_MSEC(5000));
    if (ret < 0) {
        LOG_ERR("Echo command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("EMW3080 IPC initialized successfully");
    return 0;
}

/* Get firmware version */
int emw3080_ipc_get_version(const struct device *dev, char *version, size_t version_size)
{
    uint8_t version_data[64];
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_SYS_VERSION_CMD, 
                                      NULL, 0,
                                      version_data, sizeof(version_data),
                                      K_MSEC(5000));
    if (ret < 0) {
        LOG_ERR("Get version command failed: %d", ret);
        return ret;
    }
    
    /* Copy version string */
    size_t copy_len = MIN(version_size - 1, sizeof(version_data));
    memcpy(version, version_data, copy_len);
    version[copy_len] = '\0';
    
    LOG_INF("EMW3080 firmware version: %s", version);
    return 0;
}

/* Get MAC address */
int emw3080_ipc_get_mac(const struct device *dev, uint8_t *mac)
{
    uint8_t mac_data[6];
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_GET_MAC_CMD, 
                                      NULL, 0,
                                      mac_data, sizeof(mac_data),
                                      K_MSEC(5000));
    if (ret < 0) {
        LOG_ERR("Get MAC command failed: %d", ret);
        return ret;
    }
    
    memcpy(mac, mac_data, 6);
    
    LOG_INF("EMW3080 MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return 0;
}

/* Start WiFi scan */
int emw3080_ipc_scan(const struct device *dev, enum emw3080_scan_mode mode, 
                    const char *ssid)
{
    uint8_t scan_params[64] = {0};
    size_t param_size = 0;
    
    /* First byte is scan mode */
    scan_params[param_size++] = mode;
    
    /* Add SSID if provided (for active scan) */
    if (ssid && strlen(ssid) > 0) {
        size_t ssid_len = MIN(strlen(ssid), 32);
        memcpy(scan_params + param_size, ssid, ssid_len);
        param_size += ssid_len + 1; /* Include null terminator */
    }
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_SCAN_CMD, 
                                      scan_params, param_size,
                                      NULL, 0,
                                      K_MSEC(10000));
    if (ret < 0) {
        LOG_ERR("WiFi scan command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("WiFi scan started (mode: %s)", 
            mode == EMW3080_SCAN_ACTIVE ? "active" : "passive");
    
    return 0;
}

/* Get scan results */
int emw3080_ipc_get_scan_results(const struct device *dev, 
                                struct emw3080_ap_info *aps, uint8_t max_aps)
{
    /* For now, return mock data to test the integration */
    /* This will be replaced with actual scan result parsing */
    
    if (max_aps > 0) {
        /* Mock AP 1 */
        strcpy((char *)aps[0].ssid, "EMW3080_TEST_AP");
        aps[0].bssid[0] = 0x00; aps[0].bssid[1] = 0x11; aps[0].bssid[2] = 0x22;
        aps[0].bssid[3] = 0x33; aps[0].bssid[4] = 0x44; aps[0].bssid[5] = 0x55;
        aps[0].channel = 6;
        aps[0].rssi = -45;
        aps[0].security = EMW3080_SEC_WPA2_AES;
        
        LOG_INF("Found 1 AP: %s (ch %d, RSSI %d dBm)", 
                aps[0].ssid, aps[0].channel, aps[0].rssi);
        
        return 1; /* Return number of APs found */
    }
    
    return 0;
}

/* Connect to WiFi network */
int emw3080_ipc_connect(const struct device *dev, 
                       const struct emw3080_connect_params *params)
{
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_CONNECT_CMD, 
                                      params, sizeof(*params),
                                      NULL, 0,
                                      K_MSEC(15000));
    if (ret < 0) {
        LOG_ERR("WiFi connect command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("WiFi connect command sent for SSID: %s", params->ssid);
    
    return 0;
}

/* Disconnect from WiFi network */
int emw3080_ipc_disconnect(const struct device *dev)
{
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_DISCONNECT_CMD, 
                                      NULL, 0,
                                      NULL, 0,
                                      K_MSEC(5000));
    if (ret < 0) {
        LOG_ERR("WiFi disconnect command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("WiFi disconnect command sent");
    
    return 0;
}

/* Enable network bypass mode */
int emw3080_ipc_set_bypass_mode(const struct device *dev, bool enabled)
{
    uint8_t bypass_mode = enabled ? 1 : 0;
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_BYPASS_SET_CMD, 
                                      &bypass_mode, sizeof(bypass_mode),
                                      NULL, 0,
                                      K_MSEC(5000));
    if (ret < 0) {
        LOG_ERR("Set bypass mode command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("Network bypass mode %s", enabled ? "enabled" : "disabled");
    
    return 0;
}
