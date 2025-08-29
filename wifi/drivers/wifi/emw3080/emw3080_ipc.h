/*
 * Copyright (c) 2025 David Cemin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMW3080_IPC_H
#define EMW3080_IPC_H

#include <zephyr/kernel.h>
#include <stdint.h>

/* MIPC (MXCHIP IPC) Protocol Definitions */
/* Based on analysis of ST's official MX_WIFI driver binary protocol */

/* MIPC packet structure:
 * |--------+--------+--------------------|
 * | req_id | api_id | args (<p1>...<pn>) |
 * |--------+--------+--------------------|
 * | 4Bytes | 2Bytes | nBytes             |
 * |--------+--------+--------------------|
 */

#define MIPC_PKT_REQ_ID_OFFSET      (0)
#define MIPC_PKT_REQ_ID_SIZE        (4)
#define MIPC_PKT_API_ID_OFFSET      (MIPC_PKT_REQ_ID_OFFSET + MIPC_PKT_REQ_ID_SIZE)
#define MIPC_PKT_API_ID_SIZE        (2)
#define MIPC_PKT_PARAMS_OFFSET      (MIPC_PKT_API_ID_OFFSET + MIPC_PKT_API_ID_SIZE)
#define MIPC_HEADER_SIZE            (MIPC_PKT_REQ_ID_SIZE + MIPC_PKT_API_ID_SIZE)
#define MIPC_PKT_MIN_SIZE           (MIPC_HEADER_SIZE)

/* Maximum payload size for IPC commands */
#define EMW3080_IPC_PAYLOAD_SIZE    (1600)
#define MIPC_PKT_MAX_SIZE           (MIPC_HEADER_SIZE + EMW3080_IPC_PAYLOAD_SIZE)

/* Request ID management */
#define MIPC_REQ_ID_NONE            (0x00000000)

/* API IDs for different command categories */
#define MIPC_API_ID_NONE            ((uint16_t)(0x0000))
#define MIPC_API_CMD_BASE           ((uint16_t)(MIPC_API_ID_NONE))
#define MIPC_API_EVENT_BASE         ((uint16_t)(0x8000))

/* System Commands */
#define MIPC_API_SYS_CMD_BASE       ((uint16_t)(MIPC_API_CMD_BASE + 0x0000))
#define MIPC_API_SYS_ECHO_CMD       ((uint16_t)(MIPC_API_SYS_CMD_BASE + 0x0001))
#define MIPC_API_SYS_REBOOT_CMD     ((uint16_t)(MIPC_API_SYS_CMD_BASE + 0x0002))
#define MIPC_API_SYS_VERSION_CMD    ((uint16_t)(MIPC_API_SYS_CMD_BASE + 0x0003))
#define MIPC_API_SYS_RESET_CMD      ((uint16_t)(MIPC_API_SYS_CMD_BASE + 0x0004))

/* WiFi Commands */
#define MIPC_API_WIFI_CMD_BASE      ((uint16_t)(MIPC_API_CMD_BASE + 0x0100))
#define MIPC_API_WIFI_GET_MAC_CMD   ((uint16_t)(MIPC_API_WIFI_CMD_BASE + 0x0001))
#define MIPC_API_WIFI_SCAN_CMD      ((uint16_t)(MIPC_API_WIFI_CMD_BASE + 0x0002))
#define MIPC_API_WIFI_CONNECT_CMD   ((uint16_t)(MIPC_API_WIFI_CMD_BASE + 0x0003))
#define MIPC_API_WIFI_DISCONNECT_CMD ((uint16_t)(MIPC_API_WIFI_CMD_BASE + 0x0004))
#define MIPC_API_WIFI_GET_IP_CMD    ((uint16_t)(MIPC_API_WIFI_CMD_BASE + 0x0007))
#define MIPC_API_WIFI_BYPASS_SET_CMD ((uint16_t)(MIPC_API_WIFI_CMD_BASE + 0x000c))

/* SPI Protocol Definitions */
#define EMW3080_SPI_WRITE           (0x0A)
#define EMW3080_SPI_READ            (0x0B)

/* SPI packet header structure */
struct emw3080_spi_header {
    uint8_t type;      /* SPI_WRITE or SPI_READ */
    uint16_t len;      /* Length of data */
    uint16_t lenx;     /* Length check */
    uint8_t dummy[3];  /* Padding */
} __packed;

/* MIPC packet structure */
struct emw3080_mipc_packet {
    uint32_t req_id;   /* Request ID */
    uint16_t api_id;   /* API command ID */
    uint8_t params[EMW3080_IPC_PAYLOAD_SIZE]; /* Parameters */
} __packed;

/* WiFi Security Types */
enum emw3080_security_type {
    EMW3080_SEC_NONE = 0,
    EMW3080_SEC_WEP = 1,
    EMW3080_SEC_WPA_TKIP = 2,
    EMW3080_SEC_WPA_AES = 3,
    EMW3080_SEC_WPA2_TKIP = 4,
    EMW3080_SEC_WPA2_AES = 5,
    EMW3080_SEC_WPA2_MIXED = 6,
};

/* WiFi Scan Modes */
enum emw3080_scan_mode {
    EMW3080_SCAN_ACTIVE = 0,
    EMW3080_SCAN_PASSIVE = 1,
};

/* WiFi AP Information Structure */
struct emw3080_ap_info {
    uint8_t ssid[33];        /* SSID (32 chars + null terminator) */
    uint8_t bssid[6];        /* BSSID */
    uint8_t channel;         /* Channel */
    int8_t rssi;             /* Signal strength */
    uint8_t security;        /* Security type */
    uint8_t reserved[3];     /* Padding */
} __packed;

/* WiFi Connection Parameters */
struct emw3080_connect_params {
    uint8_t ssid[33];        /* SSID */
    uint8_t password[65];    /* Password */
    uint8_t security;        /* Security type */
    uint8_t dhcp_enabled;    /* DHCP enable flag */
} __packed;

/* Function prototypes for IPC operations */
int emw3080_ipc_init(const struct device *dev);
int emw3080_ipc_send_command(const struct device *dev, uint16_t api_id, 
                            const void *params, size_t param_size,
                            void *response, size_t response_size, 
                            k_timeout_t timeout);
int emw3080_ipc_get_version(const struct device *dev, char *version, size_t version_size);
int emw3080_ipc_get_mac(const struct device *dev, uint8_t *mac);
int emw3080_ipc_scan(const struct device *dev, enum emw3080_scan_mode mode, 
                    const char *ssid);
int emw3080_ipc_get_scan_results(const struct device *dev, 
                                struct emw3080_ap_info *aps, uint8_t max_aps);
int emw3080_ipc_connect(const struct device *dev, 
                       const struct emw3080_connect_params *params);
int emw3080_ipc_disconnect(const struct device *dev);
int emw3080_ipc_set_bypass_mode(const struct device *dev, bool enabled);

#endif /* EMW3080_IPC_H */
