#include "emw3080_ipc.h"
#include "emw3080_spi.h"
#include "emw3080.h"
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(emw3080_ipc, CONFIG_WIFI_LOG_LEVEL);

/* Request ID counter for MIPC packets */
static uint32_t g_req_id = 1;

/* Get next request ID */
static uint32_t get_next_req_id(void)
{
    return g_req_id++;
}

/* Get SPI device from EMW3080 device */
static const struct device *get_spi_device(const struct device *dev)
{
    struct emw3080_data *data = (struct emw3080_data *)dev->data;
    return data->spi;
}

/* Send MIPC command and wait for response */
int emw3080_ipc_send_command(const struct device *dev, uint16_t api_id, 
                            const void *params, size_t param_size,
                            void *response, size_t response_size, 
                            k_timeout_t timeout)
{
    const struct device *spi_dev = get_spi_device(dev);
    if (!spi_dev) {
        LOG_ERR("No SPI device available");
        return -ENODEV;
    }

    struct emw3080_data *data = (struct emw3080_data *)dev->data;
    
    /* Lock SPI mutex */
    k_mutex_lock(&data->spi_mutex, K_FOREVER);
    
    /* Prepare MIPC packet */
    uint8_t tx_buffer[MIPC_PKT_MAX_SIZE];
    struct emw3080_mipc_packet *packet = (struct emw3080_mipc_packet *)tx_buffer;
    
    packet->req_id = get_next_req_id();
    packet->api_id = api_id;
    
    /* Copy parameters if provided */
    if (params && param_size > 0) {
        if (param_size > EMW3080_IPC_PAYLOAD_SIZE) {
            LOG_ERR("Parameter size too large: %zu", param_size);
            k_mutex_unlock(&data->spi_mutex);
            return -EINVAL;
        }
        memcpy(packet->params, params, param_size);
    }
    
    size_t total_size = MIPC_HEADER_SIZE + param_size;
    
    LOG_DBG("Sending MIPC command: api_id=0x%04x, req_id=%u, size=%zu", 
            api_id, packet->req_id, total_size);
    
    /* Send command via SPI */
    int ret = emw3080_spi_send_frame(spi_dev, tx_buffer, total_size);
    if (ret != 0) {
        LOG_ERR("Failed to send MIPC command: %d", ret);
        k_mutex_unlock(&data->spi_mutex);
        return ret;
    }
    
    /* Wait for response */
    uint8_t rx_buffer[MIPC_PKT_MAX_SIZE];
    size_t received_len = 0;
    
    ret = emw3080_spi_recv_frame(spi_dev, rx_buffer, sizeof(rx_buffer), &received_len);
    if (ret != 0) {
        LOG_ERR("Failed to receive MIPC response: %d", ret);
        k_mutex_unlock(&data->spi_mutex);
        return ret;
    }
    
    /* Parse response */
    if (received_len < MIPC_HEADER_SIZE) {
        LOG_ERR("Response too short: %zu bytes", received_len);
        k_mutex_unlock(&data->spi_mutex);
        return -EBADMSG;
    }
    
    struct emw3080_mipc_packet *resp = (struct emw3080_mipc_packet *)rx_buffer;
    
    LOG_DBG("Received MIPC response: api_id=0x%04x, req_id=%u, size=%zu", 
            resp->api_id, resp->req_id, received_len);
    
    /* Copy response data if requested */
    if (response && response_size > 0) {
        size_t response_data_len = received_len - MIPC_HEADER_SIZE;
        size_t copy_len = (response_data_len < response_size) ? response_data_len : response_size;
        memcpy(response, resp->params, copy_len);
    }
    
    k_mutex_unlock(&data->spi_mutex);
    return 0;
}

int emw3080_ipc_init(const struct device *dev) 
{
    LOG_INF("Initializing EMW3080 IPC layer");
    
    const struct device *spi_dev = get_spi_device(dev);
    if (!spi_dev) {
        LOG_ERR("No SPI device available for IPC");
        return -ENODEV;
    }
    
    /* Initialize SPI layer */
    int ret = emw3080_spi_init(spi_dev);
    if (ret != 0) {
        LOG_ERR("Failed to initialize SPI: %d", ret);
        return ret;
    }
    
    /* Test communication with echo command */
    const char test_data[] = "EMW3080_IPC_TEST";
    char response[32] = {0};
    
    ret = emw3080_ipc_send_command(dev, MIPC_API_SYS_ECHO_CMD, 
                                  test_data, strlen(test_data),
                                  response, sizeof(response) - 1,
                                  K_SECONDS(2));
    if (ret != 0) {
        LOG_WRN("IPC echo test failed: %d", ret);
        return ret;
    }
    
    LOG_INF("EMW3080 IPC initialized successfully");
    return 0;
}

int emw3080_ipc_scan(const struct device *dev, enum emw3080_scan_mode mode, 
                    const char *ssid) 
{
    LOG_INF("Starting WiFi scan via MIPC: mode=%d, ssid=%s", mode, ssid ? ssid : "(all)");
    
    /* Prepare scan parameters */
    struct {
        uint8_t mode;
        uint8_t ssid_len;
        char ssid[33];
        uint8_t reserved[2];
    } __packed scan_params = {0};
    
    scan_params.mode = mode;
    
    if (ssid) {
        scan_params.ssid_len = strlen(ssid);
        if (scan_params.ssid_len > 32) {
            scan_params.ssid_len = 32;
        }
        memcpy(scan_params.ssid, ssid, scan_params.ssid_len);
    }
    
    /* Send scan command */
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_SCAN_CMD,
                                      &scan_params, sizeof(scan_params),
                                      NULL, 0, K_SECONDS(10));
    if (ret != 0) {
        LOG_ERR("WiFi scan command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("WiFi scan command sent successfully");
    return 0;
}

int emw3080_ipc_get_scan_results(const struct device *dev, 
                                struct emw3080_ap_info *aps, uint8_t max_aps)
{
    if (!aps || max_aps == 0) {
        return -EINVAL;
    }
    
    LOG_DBG("Getting scan results via MIPC (max: %d APs)", max_aps);
    
    /* For now, we'll use a separate command to get results.
     * In a real implementation, this might be included in the scan response
     * or retrieved via a different API call. */
    
    uint8_t response_buffer[sizeof(struct emw3080_ap_info) * max_aps];
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_SCAN_CMD + 1, /* Scan results command */
                                      &max_aps, sizeof(max_aps),
                                      response_buffer, sizeof(response_buffer),
                                      K_SECONDS(5));
    if (ret != 0) {
        LOG_ERR("Failed to get scan results: %d", ret);
        return ret;
    }
    
    /* Copy results to output buffer */
    memcpy(aps, response_buffer, sizeof(struct emw3080_ap_info) * max_aps);
    
    LOG_DBG("Retrieved scan results successfully");
    return max_aps; /* Return number of APs retrieved */
}

int emw3080_ipc_connect(const struct device *dev, 
                       const struct emw3080_connect_params *params) 
{
    if (!params) {
        return -EINVAL;
    }
    
    LOG_INF("Connecting to WiFi via MIPC: ssid=%s", params->ssid);
    
    /* Send connect command */
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_CONNECT_CMD,
                                      params, sizeof(*params),
                                      NULL, 0, K_SECONDS(15));
    if (ret != 0) {
        LOG_ERR("WiFi connect command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("WiFi connect command sent successfully");
    return 0;
}

int emw3080_ipc_disconnect(const struct device *dev) 
{
    LOG_INF("Disconnecting from WiFi via MIPC");
    
    /* Send disconnect command */
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_DISCONNECT_CMD,
                                      NULL, 0,
                                      NULL, 0, K_SECONDS(5));
    if (ret != 0) {
        LOG_ERR("WiFi disconnect command failed: %d", ret);
        return ret;
    }
    
    LOG_INF("WiFi disconnect command sent successfully");
    return 0;
}

int emw3080_ipc_get_version(const struct device *dev, char *version, size_t version_size)
{
    if (!version || version_size == 0) {
        return -EINVAL;
    }
    
    LOG_DBG("Getting firmware version via MIPC");
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_SYS_VERSION_CMD,
                                      NULL, 0,
                                      version, version_size - 1,
                                      K_SECONDS(2));
    if (ret != 0) {
        LOG_ERR("Failed to get version: %d", ret);
        return ret;
    }
    
    version[version_size - 1] = '\0'; /* Ensure null termination */
    LOG_INF("EMW3080 firmware version: %s", version);
    return 0;
}

int emw3080_ipc_get_mac(const struct device *dev, uint8_t *mac)
{
    if (!mac) {
        return -EINVAL;
    }
    
    LOG_DBG("Getting MAC address via MIPC");
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_GET_MAC_CMD,
                                      NULL, 0,
                                      mac, 6,
                                      K_SECONDS(2));
    if (ret != 0) {
        LOG_ERR("Failed to get MAC address: %d", ret);
        return ret;
    }
    
    LOG_INF("EMW3080 MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

int emw3080_ipc_set_bypass_mode(const struct device *dev, bool enabled)
{
    LOG_INF("Setting bypass mode via MIPC: %s", enabled ? "enabled" : "disabled");
    
    uint8_t mode = enabled ? 1 : 0;
    
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_BYPASS_SET_CMD,
                                      &mode, sizeof(mode),
                                      NULL, 0, K_SECONDS(2));
    if (ret != 0) {
        LOG_ERR("Failed to set bypass mode: %d", ret);
        return ret;
    }
    
    LOG_INF("Bypass mode set successfully");
    return 0;
}
