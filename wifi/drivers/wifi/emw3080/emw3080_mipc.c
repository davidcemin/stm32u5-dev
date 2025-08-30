/**
 * @file emw3080_mipc.c
 * @brief MIPC (MX Inter-Processor Communication) protocol implementation for EMW3080
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "emw3080_mipc.h"

LOG_MODULE_REGISTER(emw3080_mipc, LOG_LEVEL_DBG);

/* Global state */
static mipc_send_func_t g_send_func = NULL;
static mipc_request_t g_pending_request = {0};
static struct k_sem g_response_sem;
static struct k_mutex g_mipc_mutex;
static uint32_t g_next_req_id = 1;
static bool g_initialized = false;

/* Helper functions */
static uint32_t get_next_req_id(void)
{
    return g_next_req_id++;
}

static uint32_t get_req_id_from_packet(const uint8_t *data)
{
    return *(uint32_t *)&data[MIPC_PKT_REQ_ID_OFFSET];
}

static uint16_t get_api_id_from_packet(const uint8_t *data)
{
    return *(uint16_t *)&data[MIPC_PKT_API_ID_OFFSET];
}

static void set_req_id_in_packet(uint8_t *data, uint32_t req_id)
{
    *(uint32_t *)&data[MIPC_PKT_REQ_ID_OFFSET] = req_id;
}

static void set_api_id_in_packet(uint8_t *data, uint16_t api_id)
{
    *(uint16_t *)&data[MIPC_PKT_API_ID_OFFSET] = api_id;
}

/* MIPC Implementation */

int32_t mipc_init(mipc_send_func_t send_func)
{
    if (send_func == NULL) {
        return MIPC_CODE_ERROR;
    }

    if (g_initialized) {
        LOG_WRN("MIPC already initialized");
        return MIPC_CODE_SUCCESS;
    }

    g_send_func = send_func;
    
    /* Initialize synchronization primitives */
    k_sem_init(&g_response_sem, 0, 1);
    k_mutex_init(&g_mipc_mutex);
    
    /* Reset pending request */
    memset(&g_pending_request, 0, sizeof(g_pending_request));
    
    g_initialized = true;
    
    LOG_INF("MIPC protocol initialized");
    return MIPC_CODE_SUCCESS;
}

int32_t mipc_deinit(void)
{
    if (!g_initialized) {
        return MIPC_CODE_SUCCESS;
    }

    g_send_func = NULL;
    g_initialized = false;
    
    LOG_INF("MIPC protocol deinitialized");
    return MIPC_CODE_SUCCESS;
}

int32_t mipc_request(uint16_t api_id,
                     uint8_t *params, uint16_t params_size,
                     uint8_t *response_buffer, uint16_t *response_size,
                     uint32_t timeout_ms)
{
    if (!g_initialized || g_send_func == NULL) {
        LOG_ERR("MIPC not initialized");
        return MIPC_CODE_ERROR;
    }

    if (params_size > (MIPC_PKT_MAX_SIZE - MIPC_HEADER_SIZE)) {
        LOG_ERR("Params too large: %d bytes", params_size);
        return MIPC_CODE_ERROR;
    }

    /* Lock to ensure single request at a time */
    if (k_mutex_lock(&g_mipc_mutex, K_MSEC(timeout_ms)) != 0) {
        LOG_ERR("Failed to acquire MIPC mutex");
        return MIPC_CODE_TIMEOUT;
    }

    int32_t result = MIPC_CODE_ERROR;
    uint8_t *packet_buffer = NULL;
    uint16_t packet_size = MIPC_HEADER_SIZE + params_size;

    /* Allocate packet buffer */
    packet_buffer = k_malloc(packet_size);
    if (packet_buffer == NULL) {
        LOG_ERR("Failed to allocate packet buffer");
        result = MIPC_CODE_NO_MEMORY;
        goto cleanup;
    }

    /* Get unique request ID */
    uint32_t req_id = get_next_req_id();

    /* Build packet */
    set_req_id_in_packet(packet_buffer, req_id);
    set_api_id_in_packet(packet_buffer, api_id);
    
    if (params_size > 0 && params != NULL) {
        memcpy(&packet_buffer[MIPC_PKT_PARAMS_OFFSET], params, params_size);
    }

    /* Setup pending request */
    g_pending_request.req_id = req_id;
    g_pending_request.api_id = api_id;
    g_pending_request.timeout_ms = timeout_ms;
    g_pending_request.response_buffer = response_buffer;
    g_pending_request.response_size = response_size;
    g_pending_request.response_received = false;

    LOG_DBG("Sending MIPC request: req_id=0x%08x, api_id=0x%04x, size=%d", 
            req_id, api_id, packet_size);

    /* Send packet */
    int send_result = g_send_func(packet_buffer, packet_size);
    if (send_result != 0) {
        LOG_ERR("Failed to send MIPC packet: %d", send_result);
        result = MIPC_CODE_ERROR;
        goto cleanup;
    }

    /* Wait for response */
    if (k_sem_take(&g_response_sem, K_MSEC(timeout_ms)) != 0) {
        LOG_ERR("MIPC request timeout: req_id=0x%08x, api_id=0x%04x", req_id, api_id);
        result = MIPC_CODE_TIMEOUT;
        goto cleanup;
    }

    if (g_pending_request.response_received) {
        LOG_DBG("MIPC request completed: req_id=0x%08x", req_id);
        result = MIPC_CODE_SUCCESS;
    } else {
        LOG_ERR("MIPC request failed: req_id=0x%08x", req_id);
        result = MIPC_CODE_ERROR;
    }

cleanup:
    /* Reset pending request */
    memset(&g_pending_request, 0, sizeof(g_pending_request));
    
    if (packet_buffer != NULL) {
        k_free(packet_buffer);
    }
    
    k_mutex_unlock(&g_mipc_mutex);
    return result;
}

void mipc_process_received_data(uint8_t *data, uint16_t size)
{
    if (!g_initialized || data == NULL || size < MIPC_PKT_MIN_SIZE) {
        LOG_WRN("Invalid received data: size=%d", size);
        return;
    }

    uint32_t req_id = get_req_id_from_packet(data);
    uint16_t api_id = get_api_id_from_packet(data);

    LOG_DBG("Received MIPC packet: req_id=0x%08x, api_id=0x%04x, size=%d", 
            req_id, api_id, size);

    /* Check if this is a response to our pending request */
    if (g_pending_request.req_id == req_id && g_pending_request.req_id != 0) {
        LOG_DBG("Matching response received for req_id=0x%08x", req_id);

        /* Copy response data if buffer provided */
        if (g_pending_request.response_buffer != NULL && 
            g_pending_request.response_size != NULL) {
            
            uint16_t response_data_size = size - MIPC_HEADER_SIZE;
            uint16_t copy_size = MIN(response_data_size, *g_pending_request.response_size);
            
            if (copy_size > 0) {
                memcpy(g_pending_request.response_buffer, 
                       &data[MIPC_PKT_PARAMS_OFFSET], copy_size);
            }
            
            *g_pending_request.response_size = copy_size;
        }

        g_pending_request.response_received = true;
        k_sem_give(&g_response_sem);
    } else if (api_id & MIPC_API_EVENT_BASE) {
        /* This is an event, not a response */
        LOG_DBG("Received MIPC event: api_id=0x%04x", api_id);
        /* TODO: Handle events if needed */
    } else {
        LOG_WRN("Unexpected MIPC packet: req_id=0x%08x (expected=0x%08x)", 
                req_id, g_pending_request.req_id);
    }
}

void mipc_poll(uint32_t timeout_ms)
{
    /* This function should be called regularly to process incoming data
     * In our implementation, mipc_process_received_data() is called directly
     * from the SPI receive interrupt/callback, so this is mostly a placeholder
     */
    k_sleep(K_MSEC(MIN(timeout_ms, 10)));
}

int32_t mipc_echo(uint8_t *input, uint16_t input_len, 
                  uint8_t *output, uint16_t *output_len, 
                  uint32_t timeout_ms)
{
    if (input == NULL || output == NULL || output_len == NULL) {
        return MIPC_CODE_ERROR;
    }

    return mipc_request(MIPC_API_SYS_ECHO_CMD, 
                       input, input_len, 
                       output, output_len, 
                       timeout_ms);
}
