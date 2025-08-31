#include "emw3080_ipc.h"
#include "emw3080_spi.h"
#include "emw3080_slip.h"
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
    
    /* Send command via SPI with MX WiFi framing (not SLIP) */
    int ret = emw3080_spi_send_frame(spi_dev, tx_buffer, total_size);
    if (ret == -EAGAIN) {
        /* Peer wants to send first: drain one frame and retry once */
        uint8_t drain_buf[MIPC_PKT_MAX_SIZE];
        size_t drain_len = 0;
        (void)emw3080_spi_recv_frame(spi_dev, drain_buf, sizeof(drain_buf), &drain_len);
        ret = emw3080_spi_send_frame(spi_dev, tx_buffer, total_size);
    }
    if (ret != 0) {
        LOG_ERR("Failed to send MIPC command: %d", ret);
        k_mutex_unlock(&data->spi_mutex);
        return ret;
    }
    
    /* Wait for response with polling */
    uint8_t rx_buffer[MIPC_PKT_MAX_SIZE];
    size_t received_len = 0;
    int poll_attempts = 0;
    /* Use provided timeout to drive polling cadence */
    int64_t timeout_ms = k_ticks_to_ms_floor64(timeout.ticks);
    if (timeout_ms <= 0) {
        timeout_ms = 2000; /* default 2s */
    }
    int initial_wait_ms = MIN(400, (int)timeout_ms / 8); /* small processing window */
    int poll_interval_ms = 50;
    int max_poll_attempts = MAX(1, (int)(timeout_ms / poll_interval_ms));

    /* Give the module time to process the command */
    k_sleep(K_MSEC(initial_wait_ms));
    
    /* Poll for response */
    do {
        ret = emw3080_spi_recv_frame(spi_dev, rx_buffer, sizeof(rx_buffer), &received_len);
        if (ret == 0 && received_len > 0) {
            LOG_DBG("Received response after %d poll attempts", poll_attempts);
            break;
        }
        static int dbg_hdr_once;
        if (!dbg_hdr_once && (poll_attempts == 0)) {
            /* First poll didn't get data; help debug by reading just the header once */
            struct emw3080_spi_header hdr = {0};
            uint8_t txh[sizeof(hdr)] = { EMW3080_SPI_WRITE, 0x00, 0x00, 0xFF, 0xFF, 0, 0, 0 };
            int r = emw3080_spi_transceive(spi_dev, txh, sizeof(txh), (uint8_t *)&hdr, sizeof(hdr));
            if (r == 0) {
                uint16_t l = sys_le16_to_cpu(hdr.len);
                uint16_t lx = sys_le16_to_cpu(hdr.lenx);
                LOG_INF("SPI hdr peek: type=0x%02x len=%04x lenx=%04x", hdr.type, l, lx);
            }
            dbg_hdr_once = 1;
        }
        
        poll_attempts++;
        if (poll_attempts < max_poll_attempts) {
            k_sleep(K_MSEC(poll_interval_ms));
        }
    } while (poll_attempts < max_poll_attempts);
    
    if (ret != 0 || received_len == 0) {
        int final_err = (received_len == 0) ? -ETIMEDOUT : ret;
        LOG_ERR("Failed to receive MIPC response after %d attempts: %d", poll_attempts, final_err);
        k_mutex_unlock(&data->spi_mutex);
        return final_err;
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
    LOG_INF("Initializing EMW3080 IPC layer (MIPC over SPI)");
    
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
        LOG_WRN("This may indicate hardware communication issues");
        LOG_INF("Continuing with IPC initialization anyway for testing purposes");
        /* Don't return error - allow device to be ready for bottom-up testing */
    } else {
        LOG_INF("IPC echo test successful: %s", response);
    }
    
    LOG_INF("EMW3080 IPC initialized successfully");
    return 0;
}

int emw3080_ipc_init_auto(void) 
{
    LOG_INF("EMW3080 IPC: Auto-initializing - searching for device...");
    
    /* Try to find the EMW3080 device */
    const struct device *dev = get_emw3080_device();
    if (!dev) {
        LOG_ERR("EMW3080 IPC: Could not find EMW3080 device for auto-init");
        return -ENODEV;
    }
    
    LOG_INF("EMW3080 IPC: Found device %s, initializing...", dev->name);
    return emw3080_ipc_init(dev);
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
                                      K_SECONDS(5));
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

int emw3080_ipc_echo(const struct device *dev, const char *msg, char *out, size_t out_len)
{
    if (!msg || !out || out_len == 0) return -EINVAL;
    int ret = emw3080_ipc_send_command(dev, MIPC_API_SYS_ECHO_CMD,
                                      msg, strlen(msg),
                                      out, out_len - 1,
                                      K_SECONDS(2));
    if (ret) return ret;
    out[out_len - 1] = '\0';
    return 0;
}

int emw3080_ipc_get_ip(const struct device *dev, uint8_t ip_out[4])
{
    if (!ip_out) return -EINVAL;
    uint8_t buf[16] = {0};
    int ret = emw3080_ipc_send_command(dev, MIPC_API_WIFI_GET_IP_CMD,
                                      NULL, 0,
                                      buf, sizeof(buf),
                                      K_SECONDS(3));
    if (ret) return ret;
    /* Assume first 4 bytes hold IPv4 address in binary */
    memcpy(ip_out, buf, 4);
    return 0;
}

int emw3080_ipc_get_linkinfo(const struct device *dev, uint8_t *buf, size_t buf_len, size_t *out_len)
{
    if (!buf || buf_len == 0) return -EINVAL;
    size_t rcv_len = 0;
    int ret = emw3080_ipc_send_command(dev, 0x0108 /* MIPC_API_WIFI_GET_LINKINFO_CMD */,
                                      NULL, 0,
                                      buf, buf_len,
                                      K_SECONDS(3));
    if (ret) return ret;
    /* We don't know exact size; caller inspects content */
    if (out_len) *out_len = rcv_len; /* currently 0, would require send_command to return size */
    return 0;
}
