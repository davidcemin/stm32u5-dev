#include "veml6030.h"
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor/veml7700.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(veml6030_cpp, LOG_LEVEL_INF);

VEML6030::VEML6030()
    : dev_(DEVICE_DT_GET_ANY(vishay_veml7700)), lux_(0.0f)
{
    if (!device_is_ready(dev_)) {
        LOG_ERR("VEML7700 device not ready");
        return;
    }
    
    // Log which device we're actually using
    LOG_INF("Using device: %s", dev_->name);
    
    // Direct register read/write test to verify I2C communication
    LOG_INF("=== VEML7700 Register Read/Write Test ===");
    
    // We'll use a simpler approach - write known values and read them back via sensor attributes
    // First, let's try setting different gain values and reading them back
    
    LOG_INF("Testing gain register read/write...");
    
    // Test gain 1x (0x00)
    struct sensor_value test_gain = {VEML7700_ALS_GAIN_1, 0};
    if (sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &test_gain) == 0) {
        struct sensor_value readback_gain;
        if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &readback_gain) == 0) {
            LOG_INF("Gain 1x: Write=%d, Readback=%d %s", test_gain.val1, readback_gain.val1, 
                   (test_gain.val1 == readback_gain.val1) ? "✓ PASS" : "✗ FAIL");
        }
    }
    
    // Test gain 2x (0x01)
    test_gain.val1 = VEML7700_ALS_GAIN_2;
    if (sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &test_gain) == 0) {
        struct sensor_value readback_gain;
        if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &readback_gain) == 0) {
            LOG_INF("Gain 2x: Write=%d, Readback=%d %s", test_gain.val1, readback_gain.val1,
                   (test_gain.val1 == readback_gain.val1) ? "✓ PASS" : "✗ FAIL");
        }
    }
    
    // Test integration time changes
    LOG_INF("Testing integration time register read/write...");
    
    // Test 25ms
    struct sensor_value test_it = {VEML7700_ALS_IT_25, 0};
    if (sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &test_it) == 0) {
        struct sensor_value readback_it;
        if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &readback_it) == 0) {
            LOG_INF("IT 25ms: Write=%d, Readback=%d %s", test_it.val1, readback_it.val1,
                   (test_it.val1 == readback_it.val1) ? "✓ PASS" : "✗ FAIL");
        }
    }
    
    // Test 800ms
    test_it.val1 = VEML7700_ALS_IT_800;
    if (sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &test_it) == 0) {
        struct sensor_value readback_it;
        if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &readback_it) == 0) {
            LOG_INF("IT 800ms: Write=%d, Readback=%d %s", test_it.val1, readback_it.val1,
                   (test_it.val1 == readback_it.val1) ? "✓ PASS" : "✗ FAIL");
        }
    }
    
    LOG_INF("=== End Register Test ===");
    
    // Critical test: Let's check if the shutdown bit is actually set
    LOG_INF("=== Checking ALS Shutdown Status ===");
    
    // Read the current ALS_CONF register value directly through the driver
    // We'll modify the gain briefly to force a register write, then read back
    struct sensor_value current_gain;
    if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &current_gain) == 0) {
        LOG_INF("Current gain before shutdown test: %d", current_gain.val1);
    }
    
    // The VEML7700 driver doesn't expose shutdown bit directly, but we can infer it
    // If the sensor is in shutdown, changing settings and then sampling should fail or give zeros
    
    // Force a sample fetch and check timing
    LOG_INF("Testing sensor response timing...");
    uint32_t start_time = k_uptime_get_32();
    int fetch_result = sensor_sample_fetch(dev_);
    uint32_t end_time = k_uptime_get_32();
    uint32_t duration = end_time - start_time;
    
    LOG_INF("Sample fetch took %d ms, result: %d", duration, fetch_result);
    
    if (duration < 10) {
        LOG_WRN("Sample fetch was too fast (%d ms) - sensor might be in shutdown mode!", duration);
        LOG_WRN("Expected ~800ms for integration time, but got %d ms", duration);
    } else {
        LOG_INF("Sample fetch timing looks normal (%d ms)", duration);
    }
    
    // Try to read the Power Saving Mode register if possible
    // The PSM register is at address 0x03 in the VEML7700
    LOG_INF("Checking if sensor is truly active...");
    
    // Test: Change integration time to a short value and see if timing changes
    struct sensor_value short_it = {VEML7700_ALS_IT_25, 0};  // 25ms
    if (sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &short_it) == 0) {
        LOG_INF("Set integration time to 25ms for timing test");
        
        start_time = k_uptime_get_32();
        fetch_result = sensor_sample_fetch(dev_);
        end_time = k_uptime_get_32();
        duration = end_time - start_time;
        
        LOG_INF("25ms IT sample fetch took %d ms, result: %d", duration, fetch_result);
        
        if (duration < 10) {
            LOG_ERR("PROBLEM: Even with 25ms IT, sample was too fast - sensor is likely shut down!");
        }
        
        // Restore long integration time
        struct sensor_value long_it = {VEML7700_ALS_IT_800, 0};
        sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &long_it);
    }
    
    LOG_INF("=== End Shutdown Test ===");
    
    // Configure sensor for maximum sensitivity
    struct sensor_value gain_val = {VEML7700_ALS_GAIN_1, 0}; // Gain 1x (highest gain)
    struct sensor_value itime_val = {VEML7700_ALS_IT_800, 0}; // 800ms integration time (longest)
    
    if (sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &gain_val) < 0) {
        LOG_WRN("Failed to set VEML7700 gain");
    } else {
        LOG_INF("VEML7700 gain set to 1x (maximum)");
    }
    
    if (sensor_attr_set(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &itime_val) < 0) {
        LOG_WRN("Failed to set VEML7700 integration time");
    } else {
        LOG_INF("VEML7700 integration time set to 800ms (maximum)");
    }
    
    // Verify configuration by reading back the values
    struct sensor_value read_gain, read_itime;
    if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &read_gain) == 0) {
        LOG_INF("VEML7700 gain readback: %d", read_gain.val1);
    }
    if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &read_itime) == 0) {
        LOG_INF("VEML7700 integration time readback: %d", read_itime.val1);
    }
    
    // Try to force a sample to wake up the sensor if it's in shutdown
    if (sensor_sample_fetch(dev_) == 0) {
        LOG_INF("VEML7700 sample fetch successful");
    } else {
        LOG_WRN("VEML7700 sample fetch failed");
    }
    
    // Try to read different sensor channels to verify I2C communication
    struct sensor_value raw_counts, white_counts;
    
    // Try to read raw ALS counts (if available)
    if (sensor_channel_get(dev_, (enum sensor_channel)SENSOR_CHAN_VEML7700_RAW_COUNTS, &raw_counts) == 0) {
        LOG_INF("VEML7700 raw ALS counts: %d", raw_counts.val1);
    } else {
        LOG_WRN("Failed to read raw ALS counts");
    }
    
    // Try to read white channel counts (if available)  
    if (sensor_channel_get(dev_, (enum sensor_channel)SENSOR_CHAN_VEML7700_WHITE_RAW_COUNTS, &white_counts) == 0) {
        LOG_INF("VEML7700 white channel counts: %d", white_counts.val1);
    } else {
        LOG_WRN("Failed to read white channel counts");
    }
    
    // Check if device is ready and responding
    if (device_is_ready(dev_)) {
        LOG_INF("VEML7700 device reports ready state");
    } else {
        LOG_ERR("VEML7700 device reports not ready");
    }
    
    // Test reading different sensor channels to verify communication
    struct sensor_value test_gain_verify, test_itime_verify;
    if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_GAIN, &test_gain_verify) == 0) {
        LOG_INF("VEML7700 current gain setting verified: %d", test_gain_verify.val1);
    } else {
        LOG_ERR("Failed to verify gain setting - I2C communication issue");
    }
    
    if (sensor_attr_get(dev_, SENSOR_CHAN_LIGHT, (enum sensor_attribute)SENSOR_ATTR_VEML7700_ITIME, &test_itime_verify) == 0) {
        LOG_INF("VEML7700 current integration time verified: %d", test_itime_verify.val1);
    } else {
        LOG_ERR("Failed to verify integration time - I2C communication issue");
    }
    
    // Force a sample read to test the data path
    LOG_INF("Testing initial sample read...");
    sample();
    
    // Give sensor time to settle after configuration
    k_msleep(100);
    
    LOG_INF("VEML7700 ambient light sensor ready");
}

bool VEML6030::isReady() const {
    return device_is_ready(dev_);
}

bool VEML6030::sample() {
    if (!isReady()) {
        LOG_ERR("VEML7700 device not ready for sampling");
        return false;
    }

    if (sensor_sample_fetch(dev_) < 0) {
        LOG_ERR("Failed to fetch VEML7700 sample");
        return false;
    }

    struct sensor_value val;
    if (sensor_channel_get(dev_, SENSOR_CHAN_LIGHT, &val) == 0) {
        lux_ = val.val1 + (val.val2 * 0.000001f); // Handle micro-lux values
        LOG_INF("VEML7700 raw reading: val1=%d, val2=%d, calculated lux=%d.%03d", val.val1, val.val2, (int)lux_, (int)((lux_ - (int)lux_) * 1000));
        
        // Also try to get raw counts for debugging
        struct sensor_value raw_val;
        if (sensor_channel_get(dev_, (enum sensor_channel)SENSOR_CHAN_VEML7700_RAW_COUNTS, &raw_val) == 0) {
            LOG_INF("VEML7700 raw counts: %d", raw_val.val1);
        }
    } else {
        LOG_ERR("Failed to get VEML7700 sensor channel data");
        return false;
    }

    return true;
}

float VEML6030::getLux() const {
    return lux_;
}
