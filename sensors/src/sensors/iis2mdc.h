#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

/**
 * C++ wrapper for the IIS2MDC 3-axis magnetometer
 * on the B-U585I-IOT02A Discovery Kit.
 */
class IIS2MDC {
public:
    IIS2MDC();
    bool isReady() const;
    bool sample();
    float getMagX() const;
    float getMagY() const;
    float getMagZ() const;

private:
    const device *dev_;
    float mag_x_;
    float mag_y_;
    float mag_z_;
};
