/**
 * @file emw3080_hci.h
 * @brief EMW3080 Hardware Control Interface (HCI) Layer
 * 
 * The HCI layer provides a hardware abstraction for low-level communication
 * with the EMW3080 WiFi module. It sits above the SPI/SLIP transport layer
 * and below the WiFi management layer.
 * 
 * Layer Architecture:
 * WiFi Management API
 *       ↓
 * HCI (Hardware Control Interface) ← This layer
 *       ↓
 * IPC (Inter-Process Communication)
 *       ↓
 * SLIP (Serial Line Internet Protocol)
 *       ↓
 * SPI (Serial Peripheral Interface)
 */

#ifndef EMW3080_HCI_H
#define EMW3080_HCI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdint.h>
#include <stdbool.h>

/* ================================== */
/* HCI Constants and Definitions */
/* ================================== */

/* HCI Command Categories */
#define EMW3080_HCI_CAT_SYSTEM      0x00
#define EMW3080_HCI_CAT_WIFI        0x01
#define EMW3080_HCI_CAT_NETWORK     0x02
#define EMW3080_HCI_CAT_SOCKET      0x03

/* HCI System Commands */
#define EMW3080_HCI_SYS_PING        0x00
#define EMW3080_HCI_SYS_VERSION     0x01
#define EMW3080_HCI_SYS_RESET       0x02
#define EMW3080_HCI_SYS_READY       0x03

/* HCI WiFi Commands */
#define EMW3080_HCI_WIFI_GET_MAC    0x00
#define EMW3080_HCI_WIFI_SCAN       0x01
#define EMW3080_HCI_WIFI_CONNECT    0x02
#define EMW3080_HCI_WIFI_DISCONNECT 0x03
#define EMW3080_HCI_WIFI_STATUS     0x04

/* HCI Response Codes */
#define EMW3080_HCI_SUCCESS         0x00
#define EMW3080_HCI_ERROR           0x01
#define EMW3080_HCI_TIMEOUT         0x02
#define EMW3080_HCI_BUSY            0x03
#define EMW3080_HCI_NOT_READY       0x04

/* HCI Timeouts */
#define EMW3080_HCI_TIMEOUT_SHORT   K_MSEC(1000)
#define EMW3080_HCI_TIMEOUT_MEDIUM  K_MSEC(5000)
#define EMW3080_HCI_TIMEOUT_LONG    K_MSEC(15000)

/* Maximum sizes */
#define EMW3080_HCI_MAX_SSID_LEN    32
#define EMW3080_HCI_MAX_PASSWORD_LEN 64
#define EMW3080_HCI_MAX_VERSION_LEN 32

/* ================================== */
/* HCI Data Structures */
/* ================================== */

/**
 * @brief HCI command header
 */
struct emw3080_hci_header {
    uint8_t category;       /* Command category */
    uint8_t command;        /* Command ID */
    uint16_t length;        /* Payload length */
} __packed;

/**
 * @brief HCI response header
 */
struct emw3080_hci_response {
    uint8_t category;       /* Command category */
    uint8_t command;        /* Command ID */
    uint8_t status;         /* Response status */
    uint8_t reserved;       /* Reserved */
    uint16_t length;        /* Payload length */
} __packed;

/**
 * @brief WiFi MAC address structure
 */
struct emw3080_hci_mac {
    uint8_t addr[6];        /* MAC address bytes */
} __packed;

/**
 * @brief WiFi scan parameters
 */
struct emw3080_hci_scan_params {
    uint8_t active;         /* 1 = active scan, 0 = passive scan */
    uint8_t channel;        /* Channel to scan (0 = all channels) */
    uint16_t dwell_time;    /* Dwell time per channel in ms */
    char ssid[EMW3080_HCI_MAX_SSID_LEN + 1]; /* Specific SSID to scan for */
} __packed;

/**
 * @brief WiFi connection parameters
 */
struct emw3080_hci_connect_params {
    char ssid[EMW3080_HCI_MAX_SSID_LEN + 1];        /* SSID */
    char password[EMW3080_HCI_MAX_PASSWORD_LEN + 1]; /* Password */
    uint8_t security;       /* Security type */
    uint8_t dhcp_enabled;   /* DHCP enable flag */
} __packed;

/**
 * @brief WiFi status information
 */
struct emw3080_hci_wifi_status {
    uint8_t connected;      /* Connection status */
    uint8_t security;       /* Security type */
    int8_t rssi;           /* Signal strength */
    uint8_t channel;        /* Channel */
    char ssid[EMW3080_HCI_MAX_SSID_LEN + 1]; /* Connected SSID */
    uint8_t bssid[6];      /* BSSID */
} __packed;

/**
 * @brief System version information
 */
struct emw3080_hci_version {
    char firmware[EMW3080_HCI_MAX_VERSION_LEN];  /* Firmware version */
    char driver[EMW3080_HCI_MAX_VERSION_LEN];    /* Driver version */
    uint8_t api_major;      /* API major version */
    uint8_t api_minor;      /* API minor version */
} __packed;

/**
 * @brief HCI context structure
 */
struct emw3080_hci_context {
    const struct device *device;    /* Associated device */
    k_timeout_t default_timeout;    /* Default command timeout */
    bool initialized;               /* Initialization flag */
    struct k_mutex command_mutex;   /* Command serialization */
};

/* ================================== */
/* HCI API Functions */
/* ================================== */

/**
 * @brief Initialize HCI layer
 * @param dev EMW3080 device
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_init(const struct device *dev);

/**
 * @brief Auto-initialize HCI with device discovery
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_init_auto(void);

/**
 * @brief Send a low-level HCI command
 * @param dev EMW3080 device
 * @param category Command category
 * @param command Command ID
 * @param params Command parameters
 * @param param_len Parameter length
 * @param response Response buffer
 * @param response_len Response buffer size
 * @param timeout Command timeout
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_send_command(const struct device *dev,
                            uint8_t category, uint8_t command,
                            const void *params, size_t param_len,
                            void *response, size_t response_len,
                            k_timeout_t timeout);

/* ================================== */
/* HCI System Commands */
/* ================================== */

/**
 * @brief Ping the EMW3080 module
 * @param dev EMW3080 device
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_ping(const struct device *dev);

/**
 * @brief Get module version information
 * @param dev EMW3080 device
 * @param version Version information structure
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_get_version(const struct device *dev, struct emw3080_hci_version *version);

/**
 * @brief Reset the EMW3080 module
 * @param dev EMW3080 device
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_reset(const struct device *dev);

/**
 * @brief Check if module is ready
 * @param dev EMW3080 device
 * @return 0 if ready, negative error code if not ready
 */
int emw3080_hci_is_ready(const struct device *dev);

/* ================================== */
/* HCI WiFi Commands */
/* ================================== */

/**
 * @brief Get WiFi MAC address
 * @param dev EMW3080 device
 * @param mac MAC address structure
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_wifi_get_mac(const struct device *dev, struct emw3080_hci_mac *mac);

/**
 * @brief Start WiFi scan
 * @param dev EMW3080 device
 * @param params Scan parameters
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_wifi_scan(const struct device *dev, const struct emw3080_hci_scan_params *params);

/**
 * @brief Connect to WiFi network
 * @param dev EMW3080 device
 * @param params Connection parameters
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_wifi_connect(const struct device *dev, const struct emw3080_hci_connect_params *params);

/**
 * @brief Disconnect from WiFi network
 * @param dev EMW3080 device
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_wifi_disconnect(const struct device *dev);

/**
 * @brief Get WiFi connection status
 * @param dev EMW3080 device
 * @param status Status information structure
 * @return 0 on success, negative error code on failure
 */
int emw3080_hci_wifi_get_status(const struct device *dev, struct emw3080_hci_wifi_status *status);

#ifdef __cplusplus
}
#endif

#endif /* EMW3080_HCI_H */
