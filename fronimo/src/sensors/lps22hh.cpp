#include "lps22hh.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lps22hh_cpp, LOG_LEVEL_INF);

LPS22HH::LPS22HH()
    : dev_(DEVICE_DT_GET_ANY(st_lps22hh)), pressure_(0.0f), temperature_(0.0f)
{
    if (!device_is_ready(dev_)) {
        LOG_ERR("LPS22HH device not ready");
        return;
    }
    
    LOG_INF("LPS22HH device ready - configuring sensor...");
    
    // Give the sensor time to stabilize after power-on
    k_sleep(K_MSEC(100));
    
    // Set output data rate to put sensor in continuous mode
    struct sensor_value odr_val = { .val1 = 1, .val2 = 0 }; // 1 Hz
    int ret = sensor_attr_set(dev_, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_val);
    if (ret == 0) {
        LOG_INF("LPS22HH configured at 1 Hz");
    } else {
        LOG_WRN("LPS22HH ODR configuration failed: %d", ret);
    }
    
    // Configure data ready trigger
    struct sensor_trigger trig = {
        .type = SENSOR_TRIG_DATA_READY,
        .chan = SENSOR_CHAN_ALL,
    };
    
    ret = sensor_trigger_set(dev_, &trig, NULL);
    if (ret != 0) {
        LOG_WRN("LPS22HH trigger configuration failed: %d", ret);
    }
    
    // Give sensor time to start measuring
    k_sleep(K_MSEC(200));
    
    LOG_INF("LPS22HH initialization complete");
}

bool LPS22HH::isReady() const {
    return device_is_ready(dev_);
}

bool LPS22HH::sample()
{
    int ret = sensor_sample_fetch(dev_);
    if (ret < 0) {
        LOG_ERR("LPS22HH sample fetch failed: %d", ret);
        return false;
    }
    
    struct sensor_value press_val, temp_val;
    
    ret = sensor_channel_get(dev_, SENSOR_CHAN_PRESS, &press_val);
    if (ret < 0) {
        LOG_ERR("LPS22HH pressure channel get failed: %d", ret);
        return false;
    }
    
    ret = sensor_channel_get(dev_, SENSOR_CHAN_AMBIENT_TEMP, &temp_val);
    if (ret < 0) {
        LOG_ERR("LPS22HH temperature channel get failed: %d", ret);
        return false;
    }
    
    // Convert to float
    pressure_ = (float)press_val.val1 + ((float)press_val.val2 / 1000000.0f);
    temperature_ = (float)temp_val.val1 + ((float)temp_val.val2 / 1000000.0f);
    
    return true;
}

float LPS22HH::getPressure() const {
    return pressure_;
}

float LPS22HH::getTemperature() const {
    return temperature_;
}
