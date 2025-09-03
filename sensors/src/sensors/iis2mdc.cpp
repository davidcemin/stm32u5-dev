#include "iis2mdc.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(iis2mdc_cpp, LOG_LEVEL_INF);

IIS2MDC::IIS2MDC()
    : dev_(DEVICE_DT_GET_ANY(st_iis2mdc)), mag_x_(0.0f), mag_y_(0.0f), mag_z_(0.0f)
{
    if (!device_is_ready(dev_)) {
        LOG_ERR("IIS2MDC device not ready");
    }
}

bool IIS2MDC::isReady() const {
    return device_is_ready(dev_);
}

bool IIS2MDC::sample() {
    if (!isReady()) {
        return false;
    }

    if (sensor_sample_fetch(dev_) < 0) {
        LOG_ERR("Failed to fetch IIS2MDC sample");
        return false;
    }

    struct sensor_value val;

    if (sensor_channel_get(dev_, SENSOR_CHAN_MAGN_X, &val) == 0) {
        mag_x_ = sensor_value_to_double(&val);
    }

    if (sensor_channel_get(dev_, SENSOR_CHAN_MAGN_Y, &val) == 0) {
        mag_y_ = sensor_value_to_double(&val);
    }

    if (sensor_channel_get(dev_, SENSOR_CHAN_MAGN_Z, &val) == 0) {
        mag_z_ = sensor_value_to_double(&val);
    }

    return true;
}

float IIS2MDC::getMagX() const { return mag_x_; }
float IIS2MDC::getMagY() const { return mag_y_; }
float IIS2MDC::getMagZ() const { return mag_z_; }
