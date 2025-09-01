#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

/**
 * C++ wrapper for the VL53L5CX Time-of-Flight sensor
 * on the B-U585I-IOT02A Discovery Kit.
 */
class VL53L5CX {
public:
    VL53L5CX();
    bool isReady() const;
    bool sample();

    float getDistance() const;  // in millimeters

private:
    const device *dev_;
    float distance_;
};
