#include "ism330dhcx.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ism330dhcx_cpp, LOG_LEVEL_INF);

ISM330DHCX::ISM330DHCX()
    : dev_(DEVICE_DT_GET_ANY(st_ism330dhcx)),
      accel_x_(0.0f), accel_y_(0.0f), accel_z_(0.0f),
      gyro_x_(0.0f), gyro_y_(0.0f), gyro_z_(0.0f)
{
    if (!device_is_ready(dev_)) {
        LOG_ERR("ISM330DHCX device not ready");
        return;
    }

    // Configure accelerometer: enable at 104 Hz
    struct sensor_value accel_odr_val = {.val1 = 104, .val2 = 0};
    int ret = sensor_attr_set(dev_, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &accel_odr_val);
    if (ret < 0) {
        LOG_ERR("Failed to set accelerometer ODR: %d", ret);
    } else {
        LOG_INF("Accelerometer configured at 104 Hz");
    }

    // Configure gyroscope: enable at 104 Hz
    struct sensor_value gyro_odr_val = {.val1 = 104, .val2 = 0};
    ret = sensor_attr_set(dev_, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &gyro_odr_val);
    if (ret < 0) {
        LOG_ERR("Failed to set gyroscope ODR: %d", ret);
    } else {
        LOG_INF("Gyroscope configured at 104 Hz");
    }
}

bool ISM330DHCX::isReady() const {
    return device_is_ready(dev_);
}

bool ISM330DHCX::sample() {
    if (!isReady()) {
        LOG_ERR("ISM330DHCX device not ready for sampling");
        return false;
    }

    int ret = sensor_sample_fetch(dev_);
    if (ret < 0) {
        LOG_ERR("Failed to fetch ISM330DHCX sample: %d", ret);
        return false;
    }

    struct sensor_value val;

    // Sample accelerometer data
    if (sensor_channel_get(dev_, SENSOR_CHAN_ACCEL_X, &val) == 0) {
        accel_x_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }
    if (sensor_channel_get(dev_, SENSOR_CHAN_ACCEL_Y, &val) == 0) {
        accel_y_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }
    if (sensor_channel_get(dev_, SENSOR_CHAN_ACCEL_Z, &val) == 0) {
        accel_z_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }

    // Sample gyroscope data
    if (sensor_channel_get(dev_, SENSOR_CHAN_GYRO_X, &val) == 0) {
        gyro_x_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }
    if (sensor_channel_get(dev_, SENSOR_CHAN_GYRO_Y, &val) == 0) {
        gyro_y_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }
    if (sensor_channel_get(dev_, SENSOR_CHAN_GYRO_Z, &val) == 0) {
        gyro_z_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }

    return true;
}

float ISM330DHCX::getAccelX() const { return accel_x_; }
float ISM330DHCX::getAccelY() const { return accel_y_; }
float ISM330DHCX::getAccelZ() const { return accel_z_; }

float ISM330DHCX::getGyroX() const { return gyro_x_; }
float ISM330DHCX::getGyroY() const { return gyro_y_; }
float ISM330DHCX::getGyroZ() const { return gyro_z_; }
