#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>

/**
 * C++ wrapper for the dual MP23DB01 PDM microphones
 * on the B-U585I-IOT02A Discovery Kit.
 *
 * NOTE: This is currently a placeholder implementation.
 * STM32U5 MDF/ADF driver support is not yet available in Zephyr.
 * 
 * Hardware connections:
 * - MP23DB01HPTR microphones connected to:
 *   - CLK0: PE9, SDIN0: PE10
 *   - CLK1: PF10, SDIN1: PF9
 */
class PDMMicrophones {
public:
    PDMMicrophones();
    bool isReady() const;

    bool configure(uint32_t pcm_rate = 16000, uint8_t channels = 1);
    bool start();
    bool stop();

    /* Non-blocking read into user buffer */
    size_t read(int16_t *buffer, size_t samples);

private:
    const device *dev_;
    bool running_;
};
