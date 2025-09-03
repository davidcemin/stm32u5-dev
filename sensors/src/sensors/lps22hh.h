#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

/**
 * C++ wrapper for the LPS22HH pressure sensor
 * on the B-U585I-IOT02A Discovery Kit.
 * Optimized for low power operation with one-shot mode.
 */
class LPS22HH {
public:
    enum class Mode {
        ONE_SHOT,    // Power-efficient: sensor sleeps between measurements
        CONTINUOUS   // Always measuring: higher power but faster response
    };

    LPS22HH();
    bool isReady() const;
    bool sample();

    // Power management
    bool setMode(Mode mode);
    bool triggerMeasurement();  // For one-shot mode
    bool powerDown();
    bool powerUp();

    float getPressure() const;     // kPa
    float getTemperature() const;  // °C

private:
    const device *dev_;
    float pressure_;
    float temperature_;
    Mode current_mode_;
    
    bool configureOneShot();
    bool configureContinuous(int frequency_hz = 1);
};
