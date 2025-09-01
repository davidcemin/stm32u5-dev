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
        LOG_ERR("VL53L5CX device not ready");
        return false;
    }

    int ret = sensor_sample_fetch(dev_);
    if (ret < 0) {
        LOG_DBG("VL53L5CX sample fetch returned: %d (expected without ULD firmware)", ret);
        return false;
    }

    struct sensor_value val;
    ret = sensor_channel_get(dev_, SENSOR_CHAN_DISTANCE, &val);
    if (ret < 0) {
        LOG_ERR("Failed to get VL53L5CX distance channel, error: %d", ret);
        return false;
    }
    
    distance_ = val.val1 + (val.val2 / 1000000.0f);
    
    /* Note: Distance will be 0.00 mm without ULD firmware */
    if (distance_ == 0.0f) {
        LOG_DBG("VL53L5CX hardware detected but requires ULD firmware for measurements");
    }

    return true;
}

float VL53L5CX::getDistance() const {
    return distance_;
}
