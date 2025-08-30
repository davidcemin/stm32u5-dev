/**
 * @file emw3080_hci.c
 * @brief EMW3080 Hardware Control Interface (HCI) Implementation
 * 
 * This module provides a hardware abstraction layer for communicating
 * with the EMW3080 WiFi module. It translates high-level commands into
 * low-level IPC messages and handles responses.
 */

#include "emw3080_hci.h"
#include "emw3080_ipc.h"
#include "emw3080.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(emw3080_hci, CONFIG_LOG_DEFAULT_LEVEL);

/* Global HCI context */
static struct emw3080_hci_context g_hci_ctx = {0};

/* ================================== */
/* Internal Helper Functions */
/* ================================== */

/**
 * @brief Convert HCI command to IPC API ID
 */
static uint16_t hci_to_ipc_api_id(uint8_t category, uint8_t command)
{
    switch (category) {
    case EMW3080_HCI_CAT_SYSTEM:
        switch (command) {
        case EMW3080_HCI_SYS_PING:
            return MIPC_API_SYS_ECHO_CMD;
        case EMW3080_HCI_SYS_VERSION:
            return MIPC_API_SYS_VERSION_CMD;
        case EMW3080_HCI_SYS_RESET:
            return MIPC_API_SYS_RESET_CMD;
        default:
            return MIPC_API_ID_NONE;
        }
        
    case EMW3080_HCI_CAT_WIFI:
        switch (command) {
        case EMW3080_HCI_WIFI_GET_MAC:
            return MIPC_API_WIFI_GET_MAC_CMD;
        case EMW3080_HCI_WIFI_SCAN:
            return MIPC_API_WIFI_SCAN_CMD;
        case EMW3080_HCI_WIFI_CONNECT:
            return MIPC_API_WIFI_CONNECT_CMD;
        case EMW3080_HCI_WIFI_DISCONNECT:
            return MIPC_API_WIFI_DISCONNECT_CMD;
        case EMW3080_HCI_WIFI_STATUS:
            return MIPC_API_WIFI_GET_IP_CMD; /* Reuse IP command for status */
        default:
            return MIPC_API_ID_NONE;
        }
        
    default:
        return MIPC_API_ID_NONE;
    }
}

/* ================================== */
/* HCI Core Functions */
/* ================================== */

int emw3080_hci_init(const struct device *dev)
{
    if (!dev) {
        LOG_ERR("Invalid device");
        return -EINVAL;
    }
    
    LOG_INF("Initializing EMW3080 HCI layer...");
    
    /* Initialize context */
    memset(&g_hci_ctx, 0, sizeof(g_hci_ctx));
    g_hci_ctx.device = dev;
    g_hci_ctx.default_timeout = EMW3080_HCI_TIMEOUT_MEDIUM;
    
    /* Initialize command mutex */
    k_mutex_init(&g_hci_ctx.command_mutex);
    
    /* Initialize underlying IPC layer */
    int ret = emw3080_ipc_init(dev);
    if (ret != 0) {
        LOG_ERR("IPC initialization failed: %d", ret);
        return ret;
    }
    
    g_hci_ctx.initialized = true;
    LOG_INF("✅ EMW3080 HCI layer initialized successfully");
    
    return 0;
}

int emw3080_hci_init_auto(void)
{
    LOG_INF("Auto-initializing EMW3080 HCI layer...");
    
    /* In test mode, initialize without requiring a device */
    g_hci_ctx.device = NULL;  /* Acceptable for testing mode */
    g_hci_ctx.default_timeout = EMW3080_HCI_TIMEOUT_MEDIUM;
    k_mutex_init(&g_hci_ctx.command_mutex);
    g_hci_ctx.initialized = true;
    
    LOG_INF("✅ EMW3080 HCI layer initialized in test mode");
    LOG_INF("Note: Device is NULL - HCI commands will test protocol handling only");
    
    return 0;
}

int emw3080_hci_send_command(const struct device *dev,
                            uint8_t category, uint8_t command,
                            const void *params, size_t param_len,
                            void *response, size_t response_len,
                            k_timeout_t timeout)
{
    if (!g_hci_ctx.initialized) {
        LOG_ERR("HCI not initialized");
        return -ENODEV;
    }
    
    /* Convert HCI command to IPC API ID */
    uint16_t api_id = hci_to_ipc_api_id(category, command);
    if (api_id == MIPC_API_ID_NONE) {
        LOG_ERR("Unsupported HCI command: cat=%02x, cmd=%02x", category, command);
        return -ENOTSUP;
    }
    
    LOG_DBG("HCI command: cat=%02x, cmd=%02x -> api_id=%04x", 
            category, command, api_id);
    
    /* Lock command mutex for serialization */
    k_mutex_lock(&g_hci_ctx.command_mutex, K_FOREVER);
    
    /* In test mode (no device), simulate command processing */
    int ret;
    if (dev == NULL) {
        LOG_INF("HCI Test Mode: Command cat=%02x, cmd=%02x (no hardware execution)", 
                category, command);
        
        /* Simulate successful protocol handling */
        ret = 0;
        
        /* Provide mock responses for specific commands */
        if (category == EMW3080_HCI_CAT_SYSTEM && command == EMW3080_HCI_SYS_PING) {
            if (response && response_len >= 4) {
                /* Echo back the ping data */
                if (params && param_len >= 4) {
                    memcpy(response, params, 4);
                }
            }
        } else if (category == EMW3080_HCI_CAT_SYSTEM && command == EMW3080_HCI_SYS_VERSION) {
            if (response && response_len >= 16) {
                strcpy((char *)response, "EMW3080-TEST-1.0");
            }
        } else if (category == EMW3080_HCI_CAT_WIFI && command == EMW3080_HCI_WIFI_GET_MAC) {
            if (response && response_len >= 6) {
                uint8_t *mac = (uint8_t *)response;
                mac[0] = 0x00; mac[1] = 0x50; mac[2] = 0xC2;
                mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x56;
            }
        }
    } else {
        /* Send real IPC command when device is available */
        ret = emw3080_ipc_send_command(dev, api_id, params, param_len,
                                      response, response_len, timeout);
    }
    
    k_mutex_unlock(&g_hci_ctx.command_mutex);
    
    if (ret != 0) {
        LOG_ERR("HCI command failed: %d", ret);
        return ret;
    }
    
    LOG_DBG("HCI command completed successfully");
    return 0;
}

/* ================================== */
/* HCI System Commands */
/* ================================== */

int emw3080_hci_ping(const struct device *dev)
{
    LOG_INF("HCI: Pinging EMW3080 module...");
    
    uint8_t ping_data[] = {0x01, 0x02, 0x03, 0x04}; /* Simple test pattern */
    uint8_t response[4];
    
    int ret = emw3080_hci_send_command(dev, 
                                      EMW3080_HCI_CAT_SYSTEM, 
                                      EMW3080_HCI_SYS_PING,
                                      ping_data, sizeof(ping_data),
                                      response, sizeof(response),
                                      EMW3080_HCI_TIMEOUT_SHORT);
    
    if (ret == 0) {
        /* Verify echo response */
        if (memcmp(ping_data, response, sizeof(ping_data)) == 0) {
            LOG_INF("✅ EMW3080 ping successful");
        } else {
            LOG_WRN("EMW3080 ping response mismatch");
            ret = -EIO;
        }
    } else {
        LOG_ERR("❌ EMW3080 ping failed: %d", ret);
    }
    
    return ret;
}

int emw3080_hci_get_version(const struct device *dev, struct emw3080_hci_version *version)
{
    if (!version) {
        return -EINVAL;
    }
    
    LOG_INF("HCI: Getting module version...");
    
    char version_str[EMW3080_HCI_MAX_VERSION_LEN];
    int ret = emw3080_hci_send_command(dev,
                                      EMW3080_HCI_CAT_SYSTEM,
                                      EMW3080_HCI_SYS_VERSION,
                                      NULL, 0,
                                      version_str, sizeof(version_str),
                                      EMW3080_HCI_TIMEOUT_SHORT);
    
    if (ret == 0) {
        /* Parse version string and populate structure */
        memset(version, 0, sizeof(*version));
        strncpy(version->firmware, version_str, sizeof(version->firmware) - 1);
        strncpy(version->driver, "EMW3080-Zephyr-1.0", sizeof(version->driver) - 1);
        version->api_major = 1;
        version->api_minor = 0;
        
        LOG_INF("✅ EMW3080 version: %s", version_str);
    } else {
        LOG_ERR("❌ Failed to get EMW3080 version: %d", ret);
    }
    
    return ret;
}

int emw3080_hci_reset(const struct device *dev)
{
    LOG_INF("HCI: Resetting EMW3080 module...");
    
    int ret = emw3080_hci_send_command(dev,
                                      EMW3080_HCI_CAT_SYSTEM,
                                      EMW3080_HCI_SYS_RESET,
                                      NULL, 0,
                                      NULL, 0,
                                      EMW3080_HCI_TIMEOUT_LONG);
    
    if (ret == 0) {
        LOG_INF("✅ EMW3080 reset successful");
        /* Give module time to restart */
        k_sleep(K_MSEC(2000));
    } else {
        LOG_ERR("❌ EMW3080 reset failed: %d", ret);
    }
    
    return ret;
}

int emw3080_hci_is_ready(const struct device *dev)
{
    /* Use ping to check if module is ready */
    return emw3080_hci_ping(dev);
}

/* ================================== */
/* HCI WiFi Commands */
/* ================================== */

int emw3080_hci_wifi_get_mac(const struct device *dev, struct emw3080_hci_mac *mac)
{
    if (!mac) {
        return -EINVAL;
    }
    
    LOG_INF("HCI: Getting WiFi MAC address...");
    
    uint8_t mac_addr[6];
    int ret = emw3080_hci_send_command(dev,
                                      EMW3080_HCI_CAT_WIFI,
                                      EMW3080_HCI_WIFI_GET_MAC,
                                      NULL, 0,
                                      mac_addr, sizeof(mac_addr),
                                      EMW3080_HCI_TIMEOUT_SHORT);
    
    if (ret == 0) {
        memcpy(mac->addr, mac_addr, 6);
        LOG_INF("✅ WiFi MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                mac->addr[0], mac->addr[1], mac->addr[2],
                mac->addr[3], mac->addr[4], mac->addr[5]);
    } else {
        LOG_ERR("❌ Failed to get WiFi MAC: %d", ret);
    }
    
    return ret;
}

int emw3080_hci_wifi_scan(const struct device *dev, const struct emw3080_hci_scan_params *params)
{
    if (!params) {
        return -EINVAL;
    }
    
    LOG_INF("HCI: Starting WiFi scan...");
    
    /* Convert HCI scan params to IPC format */
    /* For now, use simplified scan without specific parameters */
    int ret = emw3080_hci_send_command(dev,
                                      EMW3080_HCI_CAT_WIFI,
                                      EMW3080_HCI_WIFI_SCAN,
                                      NULL, 0,  /* No params for basic scan */
                                      NULL, 0,  /* Results retrieved separately */
                                      EMW3080_HCI_TIMEOUT_LONG);
    
    if (ret == 0) {
        LOG_INF("✅ WiFi scan started successfully");
    } else {
        LOG_ERR("❌ WiFi scan failed: %d", ret);
    }
    
    return ret;
}

int emw3080_hci_wifi_connect(const struct device *dev, const struct emw3080_hci_connect_params *params)
{
    if (!params) {
        return -EINVAL;
    }
    
    LOG_INF("HCI: Connecting to WiFi network: %s", params->ssid);
    
    /* Convert HCI connect params to IPC format */
    struct emw3080_connect_params ipc_params;
    memset(&ipc_params, 0, sizeof(ipc_params));
    strncpy((char *)ipc_params.ssid, params->ssid, sizeof(ipc_params.ssid) - 1);
    strncpy((char *)ipc_params.password, params->password, sizeof(ipc_params.password) - 1);
    ipc_params.security = params->security;
    ipc_params.dhcp_enabled = params->dhcp_enabled;
    
    int ret = emw3080_hci_send_command(dev,
                                      EMW3080_HCI_CAT_WIFI,
                                      EMW3080_HCI_WIFI_CONNECT,
                                      &ipc_params, sizeof(ipc_params),
                                      NULL, 0,
                                      EMW3080_HCI_TIMEOUT_LONG);
    
    if (ret == 0) {
        LOG_INF("✅ WiFi connection initiated");
    } else {
        LOG_ERR("❌ WiFi connection failed: %d", ret);
    }
    
    return ret;
}

int emw3080_hci_wifi_disconnect(const struct device *dev)
{
    LOG_INF("HCI: Disconnecting from WiFi network...");
    
    int ret = emw3080_hci_send_command(dev,
                                      EMW3080_HCI_CAT_WIFI,
                                      EMW3080_HCI_WIFI_DISCONNECT,
                                      NULL, 0,
                                      NULL, 0,
                                      EMW3080_HCI_TIMEOUT_MEDIUM);
    
    if (ret == 0) {
        LOG_INF("✅ WiFi disconnection successful");
    } else {
        LOG_ERR("❌ WiFi disconnection failed: %d", ret);
    }
    
    return ret;
}

int emw3080_hci_wifi_get_status(const struct device *dev, struct emw3080_hci_wifi_status *status)
{
    if (!status) {
        return -EINVAL;
    }
    
    LOG_INF("HCI: Getting WiFi status...");
    
    /* For now, return basic status - this should be enhanced */
    /* to actually query the module status */
    memset(status, 0, sizeof(*status));
    status->connected = 0;  /* Assume disconnected until proven otherwise */
    
    int ret = emw3080_hci_send_command(dev,
                                      EMW3080_HCI_CAT_WIFI,
                                      EMW3080_HCI_WIFI_STATUS,
                                      NULL, 0,
                                      status, sizeof(*status),
                                      EMW3080_HCI_TIMEOUT_SHORT);
    
    if (ret == 0) {
        LOG_INF("WiFi Status: %s", status->connected ? "Connected" : "Disconnected");
        if (status->connected) {
            LOG_INF("  SSID: %s", status->ssid);
            LOG_INF("  RSSI: %d dBm", status->rssi);
            LOG_INF("  Channel: %d", status->channel);
        }
    } else {
        LOG_ERR("❌ Failed to get WiFi status: %d", ret);
    }
    
    return ret;
}
