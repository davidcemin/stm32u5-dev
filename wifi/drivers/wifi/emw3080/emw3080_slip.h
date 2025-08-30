/**
 * @file emw3080_slip.h
 * @brief SLIP (Serial Line IP) protocol implementation for EMW3080
 * 
 * Based on STMicroelectronics MX_WIFI library SLIP implementation
 * but adapted for Zephyr RTOS.
 */

#ifndef EMW3080_SLIP_H
#define EMW3080_SLIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* ================================== */
/* SLIP Protocol Constants */
/* ================================== */

/* SLIP special characters */
#define SLIP_END        0xC0    /* Frame end marker */
#define SLIP_ESC        0xDB    /* Escape character */
#define SLIP_ESC_END    0xDC    /* Escaped frame end */
#define SLIP_ESC_ESC    0xDD    /* Escaped escape */

/* SLIP frame constraints */
#define SLIP_MAX_FRAME_SIZE     2048
#define SLIP_MIN_FRAME_SIZE     1

/* ================================== */
/* SLIP Data Structures */
/* ================================== */

/**
 * @brief SLIP decoder state
 */
typedef enum {
    SLIP_STATE_NORMAL = 0,
    SLIP_STATE_ESCAPE,
    SLIP_STATE_ERROR
} slip_state_t;

/**
 * @brief SLIP frame buffer
 */
typedef struct {
    uint8_t *data;
    uint16_t length;
    uint16_t capacity;
    bool complete;
} slip_frame_t;

/**
 * @brief SLIP decoder context
 */
typedef struct {
    slip_state_t state;
    slip_frame_t frame;
    uint8_t buffer[SLIP_MAX_FRAME_SIZE];
    uint16_t index;
} slip_decoder_t;

/* ================================== */
/* SLIP API Functions */
/* ================================== */

/**
 * @brief Initialize SLIP decoder
 * @param decoder Pointer to SLIP decoder context
 */
void emw3080_slip_decoder_init(slip_decoder_t *decoder);

/**
 * @brief Reset SLIP decoder
 * @param decoder Pointer to SLIP decoder context
 */
void emw3080_slip_decoder_reset(slip_decoder_t *decoder);

/**
 * @brief Process incoming byte through SLIP decoder
 * @param decoder Pointer to SLIP decoder context
 * @param byte Incoming byte
 * @return Pointer to completed frame, or NULL if frame not complete
 */
slip_frame_t *emw3080_slip_input_byte(slip_decoder_t *decoder, uint8_t byte);

/**
 * @brief Encode data with SLIP framing
 * @param input Input data buffer
 * @param input_len Length of input data
 * @param output Output buffer for SLIP-encoded data
 * @param output_size Size of output buffer
 * @param encoded_len Pointer to store actual encoded length
 * @return 0 on success, negative error code on failure
 */
int emw3080_slip_encode(const uint8_t *input, uint16_t input_len,
                        uint8_t *output, uint16_t output_size,
                        uint16_t *encoded_len);

/**
 * @brief Decode SLIP-framed data
 * @param input Input SLIP-encoded buffer
 * @param input_len Length of input data
 * @param output Output buffer for decoded data
 * @param output_size Size of output buffer
 * @param decoded_len Pointer to store actual decoded length
 * @return 0 on success, negative error code on failure
 */
int emw3080_slip_decode(const uint8_t *input, uint16_t input_len,
                        uint8_t *output, uint16_t output_size,
                        uint16_t *decoded_len);

/**
 * @brief Calculate maximum encoded size for given input
 * @param input_len Length of input data
 * @return Maximum possible encoded size
 */
uint16_t emw3080_slip_max_encoded_size(uint16_t input_len);

/**
 * @brief Check if byte needs escaping in SLIP
 * @param byte Byte to check
 * @return true if byte needs escaping, false otherwise
 */
bool emw3080_slip_needs_escape(uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif /* EMW3080_SLIP_H */
