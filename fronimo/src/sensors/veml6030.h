#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

/**
 * C++ wrapper for the VEML6030 / VEML3235
 * ambient light sensor on the B-U585I-IOT02A.
 */
class VEML6030 {
public:
    VEML6030();
    bool isReady() const;
    bool sample();

    float getLux() const;  // ambient light in lux

private:
    const device *dev_;
    float lux_;
};
