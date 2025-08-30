/**
 * @file emw3080_mipc.h
 * @brief MIPC (MX Inter-Processor Communication) protocol implementation for EMW3080
 */

#ifndef EMW3080_MIPC_H
#define EMW3080_MIPC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MIPC Error Codes */
#define MIPC_CODE_SUCCESS           (0)
#define MIPC_CODE_ERROR             (-1)
#define MIPC_CODE_TIMEOUT           (-2)
#define MIPC_CODE_NO_MEMORY         (-3)

/* MIPC Packet Structure */
/*
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
#define MIPC_PKT_MAX_SIZE           (MIPC_HEADER_SIZE + 1024) /* Reasonable max payload */

/* MIPC API IDs */
#define MIPC_REQ_ID_NONE            (0x00000000)
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

/* MIPC Request Structure */
typedef struct {
    uint32_t req_id;
    uint16_t api_id;
    uint16_t timeout_ms;
    uint8_t *response_buffer;
    uint16_t *response_size;
    bool response_received;
} mipc_request_t;

/* MIPC Send Function Type */
typedef int (*mipc_send_func_t)(uint8_t *data, uint16_t size);

/* MIPC Functions */

/**
 * @brief Initialize MIPC protocol
 * @param send_func Function to send data via SPI
 * @return MIPC_CODE_SUCCESS on success, error code otherwise
 */
int32_t mipc_init(mipc_send_func_t send_func);

/**
 * @brief Deinitialize MIPC protocol
 * @return MIPC_CODE_SUCCESS on success, error code otherwise
 */
int32_t mipc_deinit(void);

/**
 * @brief Send MIPC request and wait for response
 * @param api_id MIPC API command ID
 * @param params Input parameters for the command
 * @param params_size Size of input parameters
 * @param response_buffer Buffer to store response
 * @param response_size In: buffer size, Out: actual response size
 * @param timeout_ms Timeout in milliseconds
 * @return MIPC_CODE_SUCCESS on success, error code otherwise
 */
int32_t mipc_request(uint16_t api_id,
                     uint8_t *params, uint16_t params_size,
                     uint8_t *response_buffer, uint16_t *response_size,
                     uint32_t timeout_ms);

/**
 * @brief Poll for MIPC responses (call this regularly)
 * @param timeout_ms Timeout for polling operation
 */
void mipc_poll(uint32_t timeout_ms);

/**
 * @brief Process received data from SPI
 * @param data Received data buffer
 * @param size Size of received data
 */
void mipc_process_received_data(uint8_t *data, uint16_t size);

/**
 * @brief Test command - echo
 * @param input Input data to echo
 * @param input_len Length of input data
 * @param output Buffer for echoed data
 * @param output_len In: buffer size, Out: actual echoed data size
 * @param timeout_ms Timeout in milliseconds
 * @return MIPC_CODE_SUCCESS on success, error code otherwise
 */
int32_t mipc_echo(uint8_t *input, uint16_t input_len, 
                  uint8_t *output, uint16_t *output_len, 
                  uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EMW3080_MIPC_H */
