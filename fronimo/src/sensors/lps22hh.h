#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

/**
 * C++ wrapper for the LPS22HH pressure sensor
 * on the B-U585I-IOT02A Discovery Kit.
 */
class LPS22HH {
public:
    LPS22HH();
    bool isReady() const;
    bool sample();

    float getPressure() const;     // hPa
    float getTemperature() const;  // °C (optional channel)

private:
    const device *dev_;
    float pressure_;
    float temperature_;
};
