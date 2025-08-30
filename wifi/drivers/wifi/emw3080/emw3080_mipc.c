/**
 * @file emw3080_mipc.c
 * @brief MXCH (MXChip) protocol implementation for EMW3080
 * Based on the actual EMW3080 binary protocol using MXCH sync pattern
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "emw3080_mipc.h"

LOG_MODULE_REGISTER(emw3080_mxch, LOG_LEVEL_DBG);

/* Global state */
static mxch_send_func_t g_send_func = NULL;
static mxch_request_t g_pending_request = {0};
static struct k_sem g_response_sem;
static struct k_mutex g_mxch_mutex;
static uint16_t g_next_seq_id = 1;
static bool g_initialized = false;

/* MXCH sync pattern */
static const uint8_t mxch_sync_pattern[MXCH_SYNC_SIZE] = MXCH_SYNC_PATTERN;

/* Helper functions */
static uint16_t get_next_seq_id(void)
{
    return g_next_seq_id++;
}

static bool validate_mxch_packet(const uint8_t *data, uint16_t size)
{
    if (size < MXCH_HEADER_SIZE) {
        return false;
    }
    
    /* Check MXCH sync pattern */
    return (memcmp(data + MXCH_PKT_SYNC_OFFSET, mxch_sync_pattern, MXCH_SYNC_SIZE) == 0);
}

static uint16_t get_seq_id_from_packet(const uint8_t *data)
{
    return *(uint16_t *)&data[MXCH_PKT_SEQ_OFFSET];
}

static uint16_t get_cmd_id_from_packet(const uint8_t *data)
{
    return *(uint16_t *)&data[MXCH_PKT_CMD_OFFSET];
}

static uint16_t get_len_from_packet(const uint8_t *data)
{
    return *(uint16_t *)&data[MXCH_PKT_LEN_OFFSET];
}

static void build_mxch_packet(uint8_t *packet, uint16_t seq_id, uint16_t cmd_id, 
                              const uint8_t *data, uint16_t data_size)
{
    /* Copy MXCH sync pattern */
    memcpy(packet + MXCH_PKT_SYNC_OFFSET, mxch_sync_pattern, MXCH_SYNC_SIZE);
    
    /* Set sequence ID (little-endian) */
    *(uint16_t *)&packet[MXCH_PKT_SEQ_OFFSET] = seq_id;
    
    /* Set command ID (little-endian) */
    *(uint16_t *)&packet[MXCH_PKT_CMD_OFFSET] = cmd_id;
    
    /* Set data length (little-endian) */
    *(uint16_t *)&packet[MXCH_PKT_LEN_OFFSET] = data_size;
    
    /* Copy data if provided */
    if (data && data_size > 0) {
        memcpy(packet + MXCH_PKT_DATA_OFFSET, data, data_size);
    }
}

/* MXCH Implementation */

int32_t mxch_init(mxch_send_func_t send_func)
{
    if (send_func == NULL) {
        return MXCH_CODE_ERROR;
    }

    if (g_initialized) {
        LOG_WRN("MXCH already initialized");
        return MXCH_CODE_SUCCESS;
    }

    g_send_func = send_func;
    
    /* Initialize synchronization primitives */
    k_sem_init(&g_response_sem, 0, 1);
    k_mutex_init(&g_mxch_mutex);
    
    /* Reset pending request */
    memset(&g_pending_request, 0, sizeof(g_pending_request));
    
    g_initialized = true;
    
    LOG_INF("MXCH protocol initialized");
    return MXCH_CODE_SUCCESS;
}

int32_t mxch_deinit(void)
{
    if (!g_initialized) {
        return MXCH_CODE_SUCCESS;
    }

    g_send_func = NULL;
    g_initialized = false;
    
    LOG_INF("MXCH protocol deinitialized");
    return MXCH_CODE_SUCCESS;
}

int32_t mxch_request(uint16_t cmd_id,
                     uint8_t *params, uint16_t params_size,
                     uint8_t *response_buffer, uint16_t *response_size,
                     uint32_t timeout_ms)
{
    if (!g_initialized || g_send_func == NULL) {
        LOG_ERR("MXCH not initialized");
        return MXCH_CODE_ERROR;
    }

    if (params_size > MXCH_MAX_PAYLOAD_SIZE) {
        LOG_ERR("Params too large: %d bytes", params_size);
        return MXCH_CODE_ERROR;
    }

    /* Lock to ensure single request at a time */
    if (k_mutex_lock(&g_mxch_mutex, K_MSEC(timeout_ms)) != 0) {
        LOG_ERR("Failed to acquire MXCH mutex");
        return MXCH_CODE_TIMEOUT;
    }

    int32_t result = MXCH_CODE_ERROR;
    uint8_t *packet_buffer = NULL;
    uint16_t packet_size = MXCH_HEADER_SIZE + params_size;

    /* Allocate packet buffer */
    packet_buffer = k_malloc(packet_size);
    if (packet_buffer == NULL) {
        LOG_ERR("Failed to allocate packet buffer");
        result = MXCH_CODE_NO_MEMORY;
        goto cleanup;
    }

    /* Get unique sequence ID */
    uint16_t seq_id = get_next_seq_id();

    /* Build MXCH packet */
    build_mxch_packet(packet_buffer, seq_id, cmd_id, params, params_size);

    /* Setup pending request */
    g_pending_request.seq_id = seq_id;
    g_pending_request.cmd_id = cmd_id;
    g_pending_request.timeout_ms = timeout_ms;
    g_pending_request.response_buffer = response_buffer;
    g_pending_request.response_size = response_size;
    g_pending_request.response_received = false;

    LOG_DBG("Sending MXCH request: seq_id=0x%04x, cmd_id=0x%04x, size=%d", 
            seq_id, cmd_id, packet_size);

    /* Send packet */
    int send_result = g_send_func(packet_buffer, packet_size);
    if (send_result != 0) {
        LOG_ERR("Failed to send MXCH packet: %d", send_result);
        result = MXCH_CODE_ERROR;
        goto cleanup;
    }

    /* Wait for response */
    if (k_sem_take(&g_response_sem, K_MSEC(timeout_ms)) != 0) {
        LOG_ERR("MXCH request timeout: seq_id=0x%04x, cmd_id=0x%04x", seq_id, cmd_id);
        result = MXCH_CODE_TIMEOUT;
        goto cleanup;
    }

    if (g_pending_request.response_received) {
        LOG_DBG("MXCH request completed: seq_id=0x%04x", seq_id);
        result = MXCH_CODE_SUCCESS;
    } else {
        LOG_ERR("MXCH request failed: seq_id=0x%04x", seq_id);
        result = MXCH_CODE_ERROR;
    }

cleanup:
    /* Reset pending request */
    memset(&g_pending_request, 0, sizeof(g_pending_request));
    
    if (packet_buffer != NULL) {
        k_free(packet_buffer);
    }
    
    k_mutex_unlock(&g_mxch_mutex);
    return result;
}

void mxch_process_received_data(uint8_t *data, uint16_t size)
{
    if (!g_initialized || data == NULL || size < MXCH_HEADER_SIZE) {
        LOG_WRN("Invalid received data: size=%d", size);
        return;
    }

    /* Validate MXCH packet */
    if (!validate_mxch_packet(data, size)) {
        LOG_WRN("Invalid MXCH packet received");
        return;
    }

    uint16_t seq_id = get_seq_id_from_packet(data);
    uint16_t cmd_id = get_cmd_id_from_packet(data);
    uint16_t data_len = get_len_from_packet(data);

    LOG_DBG("Received MXCH packet: seq_id=0x%04x, cmd_id=0x%04x, size=%d", 
            seq_id, cmd_id, size);

    /* Check if this is a response to our pending request */
    if (g_pending_request.seq_id == seq_id && g_pending_request.seq_id != 0) {
        LOG_DBG("Matching response received for seq_id=0x%04x", seq_id);

        /* Copy response data if buffer provided */
        if (g_pending_request.response_buffer != NULL && 
            g_pending_request.response_size != NULL) {
            
            uint16_t copy_size = MIN(data_len, *g_pending_request.response_size);
            
            if (copy_size > 0 && (size >= MXCH_HEADER_SIZE + copy_size)) {
                memcpy(g_pending_request.response_buffer, 
                       &data[MXCH_PKT_DATA_OFFSET], copy_size);
            }
            
            *g_pending_request.response_size = copy_size;
        }

        g_pending_request.response_received = true;
        k_sem_give(&g_response_sem);
    } else {
        LOG_WRN("Unexpected MXCH packet: seq_id=0x%04x (expected=0x%04x)", 
                seq_id, g_pending_request.seq_id);
    }
}

void mxch_poll(uint32_t timeout_ms)
{
    /* This function should be called regularly to process incoming data
     * In our implementation, mxch_process_received_data() is called directly
     * from the SPI receive interrupt/callback, so this is mostly a placeholder
     */
    k_sleep(K_MSEC(MIN(timeout_ms, 10)));
}

int32_t mxch_echo(uint8_t *input, uint16_t input_len, 
                  uint8_t *output, uint16_t *output_len, 
                  uint32_t timeout_ms)
{
    if (input == NULL || output == NULL || output_len == NULL) {
        return MXCH_CODE_ERROR;
    }

    return mxch_request(MXCH_CMD_ECHO, 
                       input, input_len, 
                       output, output_len, 
                       timeout_ms);
}

/* Compatibility functions for existing MIPC API */
int32_t mipc_init(mipc_send_func_t send_func)
{
    return mxch_init((mxch_send_func_t)send_func);
}

int32_t mipc_deinit(void)
{
    return mxch_deinit();
}

int32_t mipc_request(uint16_t api_id,
                     uint8_t *params, uint16_t params_size,
                     uint8_t *response_buffer, uint16_t *response_size,
                     uint32_t timeout_ms)
{
    return mxch_request(api_id, params, params_size, response_buffer, response_size, timeout_ms);
}

void mipc_process_received_data(uint8_t *data, uint16_t size)
{
    mxch_process_received_data(data, size);
}

void mipc_poll(uint32_t timeout_ms)
{
    mxch_poll(timeout_ms);
}

int32_t mipc_echo(uint8_t *input, uint16_t input_len, 
                  uint8_t *output, uint16_t *output_len, 
                  uint32_t timeout_ms)
{
    return mxch_echo(input, input_len, output, output_len, timeout_ms);
}
