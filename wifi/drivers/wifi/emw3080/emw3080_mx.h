/**
 * @file emw3080_mx.h
 * @brief Zephyr-native MX implementation for EMW3080 WiFi module
 * 
 * This file implements the key concepts from STMicroelectronics' MX_WIFI library
 * but adapted specifically for Zephyr RTOS with proper integration.
 */

#ifndef EMW3080_MX_H
#define EMW3080_MX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <stdint.h>
#include <stdbool.h>

/* ================================== */
/* EMW3080 MX Constants */
/* ================================== */

#define EMW3080_MX_BUFFER_SIZE              (2048)
#define EMW3080_MX_MAX_SSID_SIZE            (32)
#define EMW3080_MX_MAX_PASSWORD_SIZE        (64)
#define EMW3080_MX_MAX_SCAN_RESULTS         (20)

/* Command timeouts */
#define EMW3080_MX_CMD_TIMEOUT_MS           (5000)
#define EMW3080_MX_SCAN_TIMEOUT_MS          (30000)
#define EMW3080_MX_CONNECT_TIMEOUT_MS       (15000)

/* ================================== */
/* EMW3080 MX Status Codes */
/* ================================== */

typedef enum {
    EMW3080_MX_STATUS_OK = 0,
    EMW3080_MX_STATUS_ERROR = -1,
    EMW3080_MX_STATUS_TIMEOUT = -2,
    EMW3080_MX_STATUS_INVALID_PARAM = -3,
    EMW3080_MX_STATUS_NOT_CONNECTED = -4,
    EMW3080_MX_STATUS_ALREADY_CONNECTED = -5,
    EMW3080_MX_STATUS_SCAN_FAILED = -6,
    EMW3080_MX_STATUS_INIT_FAILED = -7
} emw3080_mx_status_t;

/* ================================== */
/* EMW3080 MX Data Structures */
/* ================================== */

/**
 * @brief WiFi security types
 */
typedef enum {
    EMW3080_MX_SECURITY_NONE = 0,
    EMW3080_MX_SECURITY_WEP,
    EMW3080_MX_SECURITY_WPA_PSK,
    EMW3080_MX_SECURITY_WPA2_PSK,
    EMW3080_MX_SECURITY_WPA_WPA2_PSK,
    EMW3080_MX_SECURITY_WPA2_ENTERPRISE,
    EMW3080_MX_SECURITY_WPA3_PSK,
    EMW3080_MX_SECURITY_UNKNOWN
} emw3080_mx_security_t;

/**
 * @brief WiFi connection status
 */
typedef enum {
    EMW3080_MX_WIFI_STATUS_DISCONNECTED = 0,
    EMW3080_MX_WIFI_STATUS_CONNECTING,
    EMW3080_MX_WIFI_STATUS_CONNECTED,
    EMW3080_MX_WIFI_STATUS_DISCONNECTING,
    EMW3080_MX_WIFI_STATUS_ERROR
} emw3080_mx_wifi_status_t;

/**
 * @brief WiFi module information
 */
typedef struct {
    char product_name[32];
    char product_id[16];
    char firmware_version[16];
    uint8_t mac_address[6];
} emw3080_mx_module_info_t;

/**
 * @brief WiFi scan result
 */
typedef struct {
    char ssid[EMW3080_MX_MAX_SSID_SIZE + 1];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    emw3080_mx_security_t security;
} emw3080_mx_scan_result_t;

/**
 * @brief WiFi connection parameters
 */
typedef struct {
    char ssid[EMW3080_MX_MAX_SSID_SIZE + 1];
    char password[EMW3080_MX_MAX_PASSWORD_SIZE + 1];
    emw3080_mx_security_t security;
    uint8_t channel;  /* 0 for auto */
} emw3080_mx_connect_params_t;

/**
 * @brief WiFi network configuration
 */
typedef struct {
    char ssid[EMW3080_MX_MAX_SSID_SIZE + 1];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    emw3080_mx_security_t security;
    emw3080_mx_wifi_status_t status;
} emw3080_mx_network_info_t;

/**
 * @brief Main EMW3080 MX object structure
 */
typedef struct {
    /* Hardware interfaces */
    const struct device *spi_dev;
    struct gpio_dt_spec reset_gpio;
    struct gpio_dt_spec notify_gpio;
    struct gpio_dt_spec flow_gpio;
    
    /* SPI configuration */
    struct spi_config spi_cfg;
    
    /* Network interface */
    struct net_if *iface;
    
    /* Status and state */
    emw3080_mx_wifi_status_t wifi_status;
    emw3080_mx_module_info_t module_info;
    emw3080_mx_network_info_t current_network;
    
    /* Synchronization */
    struct k_mutex cmd_mutex;
    struct k_sem cmd_sem;
    struct k_sem data_sem;
    
    /* Communication buffers */
    uint8_t tx_buffer[EMW3080_MX_BUFFER_SIZE];
    uint8_t rx_buffer[EMW3080_MX_BUFFER_SIZE];
    
    /* Scan results */
    emw3080_mx_scan_result_t scan_results[EMW3080_MX_MAX_SCAN_RESULTS];
    uint8_t scan_count;
    
    /* Flags */
    bool initialized;
    bool bypass_mode;
    
} emw3080_mx_object_t;

/* ================================== */
/* EMW3080 MX API Functions */
/* ================================== */

/**
 * @brief Initialize the EMW3080 MX module
 * @param obj Pointer to EMW3080 MX object
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_init(emw3080_mx_object_t *obj);

/**
 * @brief Deinitialize the EMW3080 MX module
 * @param obj Pointer to EMW3080 MX object
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_deinit(emw3080_mx_object_t *obj);

/**
 * @brief Reset the EMW3080 module
 * @param obj Pointer to EMW3080 MX object
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_reset(emw3080_mx_object_t *obj);

/**
 * @brief Get module information
 * @param obj Pointer to EMW3080 MX object
 * @param info Pointer to store module information
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_get_module_info(emw3080_mx_object_t *obj, 
                                               emw3080_mx_module_info_t *info);

/**
 * @brief Scan for available WiFi networks
 * @param obj Pointer to EMW3080 MX object
 * @param results Array to store scan results
 * @param max_results Maximum number of results to store
 * @param actual_count Pointer to store actual number of networks found
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_scan(emw3080_mx_object_t *obj,
                                    emw3080_mx_scan_result_t *results,
                                    uint8_t max_results,
                                    uint8_t *actual_count);

/**
 * @brief Connect to a WiFi network
 * @param obj Pointer to EMW3080 MX object
 * @param params Connection parameters
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_connect(emw3080_mx_object_t *obj,
                                       const emw3080_mx_connect_params_t *params);

/**
 * @brief Disconnect from current WiFi network
 * @param obj Pointer to EMW3080 MX object
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_disconnect(emw3080_mx_object_t *obj);

/**
 * @brief Check if connected to WiFi
 * @param obj Pointer to EMW3080 MX object
 * @return true if connected, false otherwise
 */
bool emw3080_mx_is_connected(emw3080_mx_object_t *obj);

/**
 * @brief Get current network information
 * @param obj Pointer to EMW3080 MX object
 * @param network_info Pointer to store network information
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_get_network_info(emw3080_mx_object_t *obj,
                                                emw3080_mx_network_info_t *network_info);

/**
 * @brief Enable bypass mode (Zephyr handles TCP/IP stack)
 * @param obj Pointer to EMW3080 MX object
 * @param enable true to enable bypass mode, false to disable
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_set_bypass_mode(emw3080_mx_object_t *obj, bool enable);

/**
 * @brief Send data packet (in bypass mode)
 * @param obj Pointer to EMW3080 MX object
 * @param data Pointer to data to send
 * @param length Length of data
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_send_data(emw3080_mx_object_t *obj,
                                         const uint8_t *data,
                                         uint16_t length);

/**
 * @brief Receive data packet (in bypass mode)
 * @param obj Pointer to EMW3080 MX object
 * @param data Buffer to store received data
 * @param max_length Maximum length of buffer
 * @param actual_length Pointer to store actual received length
 * @param timeout_ms Timeout in milliseconds
 * @return EMW3080_MX_STATUS_OK on success, error code otherwise
 */
emw3080_mx_status_t emw3080_mx_receive_data(emw3080_mx_object_t *obj,
                                            uint8_t *data,
                                            uint16_t max_length,
                                            uint16_t *actual_length,
                                            uint32_t timeout_ms);

/* ================================== */
/* Global EMW3080 MX Object */
/* ================================== */

/**
 * @brief Get the global EMW3080 MX object instance
 * @return Pointer to the global EMW3080 MX object
 */
emw3080_mx_object_t *emw3080_mx_get_object(void);

#ifdef __cplusplus
}
#endif

#endif /* EMW3080_MX_H */
