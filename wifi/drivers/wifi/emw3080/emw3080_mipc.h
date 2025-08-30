/**
 * @file emw3080_mipc.h
 * @brief MXCH (MXChip) protocol implementation for EMW3080
 * Based on the actual EMW3080 binary protocol using MXCH sync pattern
 */

#ifndef EMW3080_MIPC_H
#define EMW3080_MIPC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MXCH Protocol Error Codes */
#define MXCH_CODE_SUCCESS           (0)
#define MXCH_CODE_ERROR             (-1)
#define MXCH_CODE_TIMEOUT           (-2)
#define MXCH_CODE_NO_MEMORY         (-3)

/* MXCH Protocol Constants */
#define MXCH_SYNC_PATTERN           {0x4D, 0x58, 0x43, 0x48}  /* "MXCH" */
#define MXCH_SYNC_SIZE              (4)
#define MXCH_SEQ_SIZE               (2)
#define MXCH_CMD_SIZE               (2)
#define MXCH_LEN_SIZE               (2)
#define MXCH_HEADER_SIZE            (MXCH_SYNC_SIZE + MXCH_SEQ_SIZE + MXCH_CMD_SIZE + MXCH_LEN_SIZE)
#define MXCH_MAX_PAYLOAD_SIZE       (1024)
#define MXCH_MAX_PACKET_SIZE        (MXCH_HEADER_SIZE + MXCH_MAX_PAYLOAD_SIZE)

/* MXCH Packet Structure */
/*
 * |--------+--------+--------+--------+------------|
 * | SYNC   | SEQ    | CMD    | LEN    | DATA       |
 * |--------+--------+--------+--------+------------|
 * | 4Bytes | 2Bytes | 2Bytes | 2Bytes | LEN Bytes  |
 * | MXCH   |        |        |        |            |
 * |--------+--------+--------+--------+------------|
 */
#define MXCH_PKT_SYNC_OFFSET        (0)
#define MXCH_PKT_SEQ_OFFSET         (MXCH_PKT_SYNC_OFFSET + MXCH_SYNC_SIZE)
#define MXCH_PKT_CMD_OFFSET         (MXCH_PKT_SEQ_OFFSET + MXCH_SEQ_SIZE)
#define MXCH_PKT_LEN_OFFSET         (MXCH_PKT_CMD_OFFSET + MXCH_CMD_SIZE)
#define MXCH_PKT_DATA_OFFSET        (MXCH_PKT_LEN_OFFSET + MXCH_LEN_SIZE)

/* MXCH Command IDs (based on existing code) */
#define MXCH_CMD_ECHO               (0x0101)  /* Echo test */
#define MXCH_CMD_VERSION            (0x0102)  /* Get version */
#define MXCH_CMD_SCAN               (0x0102)  /* WiFi scan (same as version for now) */
#define MXCH_CMD_CONNECT            (0x0103)  /* WiFi connect */
#define MXCH_CMD_DISCONNECT         (0x0104)  /* WiFi disconnect */
#define MXCH_CMD_GET_MAC            (0x0105)  /* Get MAC address */
#define MXCH_CMD_GET_STATUS         (0x0106)  /* Get WiFi status */

/* Compatibility aliases for existing MIPC code */
#define MIPC_CODE_SUCCESS           MXCH_CODE_SUCCESS
#define MIPC_CODE_ERROR             MXCH_CODE_ERROR
#define MIPC_CODE_TIMEOUT           MXCH_CODE_TIMEOUT
#define MIPC_CODE_NO_MEMORY         MXCH_CODE_NO_MEMORY

#define MIPC_API_SYS_ECHO_CMD       MXCH_CMD_ECHO
#define MIPC_API_SYS_VERSION_CMD    MXCH_CMD_VERSION
#define MIPC_API_WIFI_GET_MAC_CMD   MXCH_CMD_GET_MAC

/* MXCH Send Function Type */
typedef int (*mxch_send_func_t)(uint8_t *data, uint16_t size);

/* MXCH Request Structure */
typedef struct {
    uint16_t seq_id;
    uint16_t cmd_id;
    uint16_t timeout_ms;
    uint8_t *response_buffer;
    uint16_t *response_size;
    bool response_received;
} mxch_request_t;

/* MXCH Functions */

/**
 * @brief Initialize MXCH protocol
 * @param send_func Function to send data via SPI
 * @return MXCH_CODE_SUCCESS on success, error code otherwise
 */
int32_t mxch_init(mxch_send_func_t send_func);

/**
 * @brief Deinitialize MXCH protocol
 * @return MXCH_CODE_SUCCESS on success, error code otherwise
 */
int32_t mxch_deinit(void);

/**
 * @brief Send MXCH request and wait for response
 * @param cmd_id MXCH command ID
 * @param params Parameters to send (can be NULL)
 * @param params_size Size of parameters
 * @param response_buffer Buffer to store response (can be NULL)
 * @param response_size Pointer to response buffer size
 * @param timeout_ms Timeout in milliseconds
 * @return MXCH_CODE_SUCCESS on success, error code otherwise
 */
int32_t mxch_request(uint16_t cmd_id,
                     uint8_t *params, uint16_t params_size,
                     uint8_t *response_buffer, uint16_t *response_size,
                     uint32_t timeout_ms);

/**
 * @brief Process received MXCH data
 * @param data Received data buffer
 * @param size Size of received data
 */
void mxch_process_received_data(uint8_t *data, uint16_t size);

/**
 * @brief Poll for MXCH responses
 * @param timeout_ms Maximum time to wait
 */
void mxch_poll(uint32_t timeout_ms);

/**
 * @brief Send echo command via MXCH
 * @param input Input data
 * @param input_len Input data length
 * @param output Output buffer
 * @param output_len Output buffer size (in/out)
 * @param timeout_ms Timeout in milliseconds
 * @return MXCH_CODE_SUCCESS on success, error code otherwise
 */
int32_t mxch_echo(uint8_t *input, uint16_t input_len, 
                  uint8_t *output, uint16_t *output_len, 
                  uint32_t timeout_ms);

/* Legacy compatibility types and functions */
typedef mxch_request_t mipc_request_t;
typedef mxch_send_func_t mipc_send_func_t;

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
