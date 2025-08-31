
#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

/**
 * C++ wrapper for the HTS221 humidity & temperature sensor
 * on the B-U585I-IOT02A Discovery Kit.
 */
class HTS221 {
public:
    HTS221();
    bool isReady() const;
    bool sample();
    float getTemperature() const;
    float getHumidity() const;

private:
    const device *dev_;
    float temperature_;
    float humidity_;
};

