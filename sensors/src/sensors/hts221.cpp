
#include "hts221.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hts221_cpp, LOG_LEVEL_INF);

HTS221::HTS221()
    : dev_(DEVICE_DT_GET_ANY(st_hts221)), temperature_(0.0f), humidity_(0.0f)
{
    if (!device_is_ready(dev_)) {
        LOG_ERR("HTS221 device not ready");
    }
}

bool HTS221::isReady() const {
    return device_is_ready(dev_);
}

bool HTS221::sample() {
    if (!isReady()) {
        return false;
    }

    if (sensor_sample_fetch(dev_) < 0) {
        LOG_ERR("Failed to fetch HTS221 sample");
        return false;
    }

    struct sensor_value val;

    if (sensor_channel_get(dev_, SENSOR_CHAN_AMBIENT_TEMP, &val) == 0) {
        temperature_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }

    if (sensor_channel_get(dev_, SENSOR_CHAN_HUMIDITY, &val) == 0) {
        humidity_ = (float)val.val1 + (float)val.val2 / 1000000.0f;
    }

    return true;
}

float HTS221::getTemperature() const {
    return temperature_;
}

float HTS221::getHumidity() const {
    return humidity_;
}
