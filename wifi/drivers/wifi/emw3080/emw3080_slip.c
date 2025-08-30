/**
 * @file emw3080_slip.c
 * @brief SLIP (Serial Line IP) protocol implementation for EMW3080
 * 
 * Based on STMicroelectronics MX_WIFI library SLIP implementation
 * but adapted for Zephyr RTOS.
 */

#include "emw3080_slip.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(emw3080_slip, CONFIG_LOG_DEFAULT_LEVEL);

/* ================================== */
/* SLIP Decoder Implementation */
/* ================================== */

void emw3080_slip_decoder_init(slip_decoder_t *decoder)
{
    if (!decoder) {
        return;
    }
    
    memset(decoder, 0, sizeof(slip_decoder_t));
    decoder->state = SLIP_STATE_NORMAL;
    decoder->frame.data = decoder->buffer;
    decoder->frame.capacity = SLIP_MAX_FRAME_SIZE;
    decoder->index = 0;
}

void emw3080_slip_decoder_reset(slip_decoder_t *decoder)
{
    if (!decoder) {
        return;
    }
    
    decoder->state = SLIP_STATE_NORMAL;
    decoder->frame.length = 0;
    decoder->frame.complete = false;
    decoder->index = 0;
}

slip_frame_t *emw3080_slip_input_byte(slip_decoder_t *decoder, uint8_t byte)
{
    if (!decoder) {
        return NULL;
    }
    
    slip_frame_t *completed_frame = NULL;
    
    switch (decoder->state) {
    case SLIP_STATE_NORMAL:
        switch (byte) {
        case SLIP_END:
            /* End of frame */
            if (decoder->index > 0) {
                /* We have data - complete the frame */
                decoder->frame.length = decoder->index;
                decoder->frame.complete = true;
                completed_frame = &decoder->frame;
                LOG_DBG("SLIP frame completed, length: %d", decoder->index);
                
                /* Reset for next frame */
                emw3080_slip_decoder_reset(decoder);
            } else {
                /* Empty frame or start of frame - ignore */
                LOG_DBG("SLIP: Empty frame or frame start");
            }
            break;
            
        case SLIP_ESC:
            /* Start escape sequence */
            decoder->state = SLIP_STATE_ESCAPE;
            break;
            
        default:
            /* Normal data byte */
            if (decoder->index < SLIP_MAX_FRAME_SIZE) {
                decoder->buffer[decoder->index++] = byte;
            } else {
                LOG_WRN("SLIP frame too large, resetting");
                emw3080_slip_decoder_reset(decoder);
                decoder->state = SLIP_STATE_ERROR;
            }
            break;
        }
        break;
        
    case SLIP_STATE_ESCAPE:
        switch (byte) {
        case SLIP_ESC_END:
            /* Escaped END character */
            if (decoder->index < SLIP_MAX_FRAME_SIZE) {
                decoder->buffer[decoder->index++] = SLIP_END;
                decoder->state = SLIP_STATE_NORMAL;
            } else {
                LOG_WRN("SLIP frame too large, resetting");
                emw3080_slip_decoder_reset(decoder);
                decoder->state = SLIP_STATE_ERROR;
            }
            break;
            
        case SLIP_ESC_ESC:
            /* Escaped ESC character */
            if (decoder->index < SLIP_MAX_FRAME_SIZE) {
                decoder->buffer[decoder->index++] = SLIP_ESC;
                decoder->state = SLIP_STATE_NORMAL;
            } else {
                LOG_WRN("SLIP frame too large, resetting");
                emw3080_slip_decoder_reset(decoder);
                decoder->state = SLIP_STATE_ERROR;
            }
            break;
            
        default:
            /* Invalid escape sequence */
            LOG_WRN("Invalid SLIP escape sequence: 0x%02x", byte);
            emw3080_slip_decoder_reset(decoder);
            decoder->state = SLIP_STATE_ERROR;
            break;
        }
        break;
        
    case SLIP_STATE_ERROR:
        /* Wait for frame end to resynchronize */
        if (byte == SLIP_END) {
            LOG_DBG("SLIP resynchronized");
            emw3080_slip_decoder_reset(decoder);
        }
        break;
    }
    
    return completed_frame;
}

/* ================================== */
/* SLIP Encoding/Decoding Functions */
/* ================================== */

bool emw3080_slip_needs_escape(uint8_t byte)
{
    return (byte == SLIP_END || byte == SLIP_ESC);
}

uint16_t emw3080_slip_max_encoded_size(uint16_t input_len)
{
    /* Worst case: every byte needs escaping + 2 END markers */
    return (input_len * 2) + 2;
}

int emw3080_slip_encode(const uint8_t *input, uint16_t input_len,
                        uint8_t *output, uint16_t output_size,
                        uint16_t *encoded_len)
{
    if (!input || !output || !encoded_len) {
        return -EINVAL;
    }
    
    if (input_len == 0) {
        *encoded_len = 0;
        return 0;
    }
    
    uint16_t out_idx = 0;
    
    /* Add frame start marker */
    if (out_idx >= output_size) {
        return -ENOSPC;
    }
    output[out_idx++] = SLIP_END;
    
    /* Encode data */
    for (uint16_t i = 0; i < input_len; i++) {
        uint8_t byte = input[i];
        
        if (emw3080_slip_needs_escape(byte)) {
            /* Add escape character */
            if (out_idx >= output_size) {
                return -ENOSPC;
            }
            output[out_idx++] = SLIP_ESC;
            
            /* Add escaped byte */
            if (out_idx >= output_size) {
                return -ENOSPC;
            }
            
            if (byte == SLIP_END) {
                output[out_idx++] = SLIP_ESC_END;
            } else if (byte == SLIP_ESC) {
                output[out_idx++] = SLIP_ESC_ESC;
            }
        } else {
            /* Normal byte */
            if (out_idx >= output_size) {
                return -ENOSPC;
            }
            output[out_idx++] = byte;
        }
    }
    
    /* Add frame end marker */
    if (out_idx >= output_size) {
        return -ENOSPC;
    }
    output[out_idx++] = SLIP_END;
    
    *encoded_len = out_idx;
    
    LOG_DBG("SLIP encoded %d bytes to %d bytes", input_len, out_idx);
    return 0;
}

int emw3080_slip_decode(const uint8_t *input, uint16_t input_len,
                        uint8_t *output, uint16_t output_size,
                        uint16_t *decoded_len)
{
    if (!input || !output || !decoded_len) {
        return -EINVAL;
    }
    
    if (input_len == 0) {
        *decoded_len = 0;
        return 0;
    }
    
    slip_decoder_t decoder;
    emw3080_slip_decoder_init(&decoder);
    
    /* Process all input bytes */
    for (uint16_t i = 0; i < input_len; i++) {
        slip_frame_t *frame = emw3080_slip_input_byte(&decoder, input[i]);
        
        if (frame && frame->complete) {
            /* Frame completed */
            if (frame->length > output_size) {
                LOG_ERR("Decoded frame too large: %d > %d", frame->length, output_size);
                return -ENOSPC;
            }
            
            memcpy(output, frame->data, frame->length);
            *decoded_len = frame->length;
            
            LOG_DBG("SLIP decoded %d bytes from %d bytes", frame->length, input_len);
            return 0;
        }
    }
    
    /* No complete frame found */
    *decoded_len = 0;
    LOG_DBG("SLIP: No complete frame in input");
    return -EAGAIN;
}
