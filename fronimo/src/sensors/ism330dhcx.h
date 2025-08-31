#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

/**
 * C++ wrapper for the ISM330DHCX 6-axis IMU
 * (accelerometer + gyroscope).
 */
class ISM330DHCX {
public:
    ISM330DHCX();
    bool isReady() const;
    bool sample();

    float getAccelX() const;
    float getAccelY() const;
    float getAccelZ() const;

    float getGyroX() const;
    float getGyroY() const;
    float getGyroZ() const;

private:
    const device *dev_;
    float accel_x_, accel_y_, accel_z_;
    float gyro_x_, gyro_y_, gyro_z_;
};
