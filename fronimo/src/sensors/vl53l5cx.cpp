#include "vl53l5cx.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vl53l5cx_cpp, LOG_LEVEL_INF);

VL53L5CX::VL53L5CX()
    : dev_(DEVICE_DT_GET(DT_NODELABEL(vl53l5cx))), distance_(0.0f)
{
    if (!device_is_ready(dev_)) {
        LOG_ERR("VL53L5CX device not ready");
    }
}

bool VL53L5CX::isReady() const {
    return device_is_ready(dev_);
}

bool VL53L5CX::sample() {
    if (!isReady()) {
        return false;
    }

    if (sensor_sample_fetch(dev_) < 0) {
        LOG_ERR("Failed to fetch VL53L5CX sample");
        return false;
    }

    struct sensor_value val;
    if (sensor_channel_get(dev_, SENSOR_CHAN_DISTANCE, &val) == 0) {
        distance_ = val.val1 + (val.val2 / 1000000.0f);
    }

    return true;
}

float VL53L5CX::getDistance() const {
    return distance_;
}
