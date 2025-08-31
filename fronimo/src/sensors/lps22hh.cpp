#include "lps22hh.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(lps22hh_cpp, LOG_LEVEL_INF);

LPS22HH::LPS22HH()
    : dev_(DEVICE_DT_GET_ANY(st_lps22hh)), pressure_(0.0f), temperature_(0.0f), current_mode_(Mode::ONE_SHOT)
{
    if (!device_is_ready(dev_)) {
        LOG_ERR("LPS22HH device not ready");
        return;
    }
    
    LOG_INF("LPS22HH device ready - configuring for low power...");
    
    // Give the sensor time to stabilize after power-on
    k_sleep(K_MSEC(100));
    
    // Start in one-shot mode for power efficiency
    if (configureOneShot()) {
        LOG_INF("LPS22HH configured in one-shot mode (power optimized)");
    } else {
        LOG_WRN("LPS22HH one-shot configuration failed, falling back to continuous");
        configureContinuous(1);
        current_mode_ = Mode::CONTINUOUS;
    }
    
    LOG_INF("LPS22HH initialization complete");
}

bool LPS22HH::isReady() const {
    return device_is_ready(dev_);
}

bool LPS22HH::sample()
{
    // In one-shot mode, trigger a measurement first
    if (current_mode_ == Mode::ONE_SHOT) {
        if (!triggerMeasurement()) {
            LOG_ERR("LPS22HH one-shot trigger failed");
            return false;
        }
        
        // Wait for measurement to complete (typical conversion time ~5ms)
        k_sleep(K_MSEC(10));
    }
    
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

bool LPS22HH::setMode(Mode mode)
{
    if (mode == current_mode_) {
        return true; // Already in the requested mode
    }
    
    bool success = false;
    if (mode == Mode::ONE_SHOT) {
        success = configureOneShot();
        if (success) {
            LOG_INF("LPS22HH switched to one-shot mode");
        }
    } else {
        success = configureContinuous(1);
        if (success) {
            LOG_INF("LPS22HH switched to continuous mode (1 Hz)");
        }
    }
    
    if (success) {
        current_mode_ = mode;
    }
    
    return success;
}

bool LPS22HH::triggerMeasurement()
{
    // For one-shot mode, we trigger by setting ODR briefly then back to 0
    struct sensor_value odr_val = { .val1 = 1, .val2 = 0 }; // Trigger measurement
    int ret = sensor_attr_set(dev_, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_val);
    
    if (ret == 0) {
        // Immediately set back to 0 to maintain one-shot behavior
        k_sleep(K_MSEC(1));
        odr_val.val1 = 0;
        sensor_attr_set(dev_, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_val);
        return true;
    }
    
    return false;
}

bool LPS22HH::powerDown()
{
    struct sensor_value power_val = { .val1 = 0, .val2 = 0 }; // Power down
    int ret = sensor_attr_set(dev_, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &power_val);
    
    if (ret == 0) {
        LOG_INF("LPS22HH powered down");
        return true;
    }
    
    LOG_ERR("LPS22HH power down failed: %d", ret);
    return false;
}

bool LPS22HH::powerUp()
{
    // Power up and restore the current mode
    k_sleep(K_MSEC(50)); // Allow sensor to stabilize
    
    if (current_mode_ == Mode::ONE_SHOT) {
        return configureOneShot();
    } else {
        return configureContinuous(1);
    }
}

bool LPS22HH::configureOneShot()
{
    // Set ODR to 0 (power down / one-shot mode)
    struct sensor_value odr_val = { .val1 = 0, .val2 = 0 };
    int ret = sensor_attr_set(dev_, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_val);
    
    return (ret == 0);
}

bool LPS22HH::configureContinuous(int frequency_hz)
{
    // Set output data rate for continuous mode
    struct sensor_value odr_val = { .val1 = frequency_hz, .val2 = 0 };
    int ret = sensor_attr_set(dev_, SENSOR_CHAN_ALL, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_val);
    
    if (ret == 0) {
        // Configure data ready trigger for continuous mode
        struct sensor_trigger trig = {
            .type = SENSOR_TRIG_DATA_READY,
            .chan = SENSOR_CHAN_ALL,
        };
        
        sensor_trigger_set(dev_, &trig, NULL); // Ignore errors for trigger
        k_sleep(K_MSEC(200)); // Allow sensor to stabilize
        return true;
    }
    
    return false;
}

float LPS22HH::getPressure() const {
    return pressure_;
}

float LPS22HH::getTemperature() const {
    return temperature_;
}
