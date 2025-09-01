#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include "sensors/hts221.h"
#include "sensors/iis2mdc.h"
#include "sensors/ism330dhcx.h"
#include "sensors/lps22hh.h"
#include "sensors/vl53l5cx.h"
#include "sensors/veml6030.h"
#include "sensors/pdm_mics.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("B-U585I-IOT02A C++ Sensor Example");

    HTS221 hts;
    IIS2MDC mag;
    ISM330DHCX imu;
    LPS22HH lps;
    VL53L5CX tof;  // Hardware detected ✅, measurements require ULD firmware
    VEML6030 als;
    PDMMicrophones mics; // Placeholder - driver not available yet

    if (!hts.isReady()) {
        LOG_ERR("HTS221 not available");
        return 0;
    }

    if (!mag.isReady()) {
        LOG_ERR("IIS2MDC not available");
        return 0;
    }
    if (!imu.isReady()) {
        LOG_ERR("ISM330DHCX not available");
        return 0;
    }
    if(!lps.isReady()) {
        LOG_ERR("LPS22HH not available");
        return 0;
    }

    if(!tof.isReady()) {
        LOG_WRN("VL53L5CX not available (expected until physical sensor connected)");
    }

    if(!als.isReady()) {
        LOG_ERR("VEML7700 (ALS) not available");
        return 0;
    }

    lps.setMode(LPS22HH::Mode::ONE_SHOT);

    while (true) {
        if (hts.sample()) {
            float temp = hts.getTemperature();
            float hum = hts.getHumidity();
            LOG_INF("Temperature: %d.%02d °C, Humidity: %d.%02d %%", 
                    (int)temp, (int)((temp - (int)temp) * 100),
                    (int)hum, (int)((hum - (int)hum) * 100));
        }
        if (mag.sample()) {
            float magX = mag.getMagX();
            float magY = mag.getMagY();
            float magZ = mag.getMagZ();
            LOG_INF("Mag [uT]: X=%d.%02d, Y=%d.%02d, Z=%d.%02d",
                    (int)magX, (int)((magX - (int)magX) * 100),
                    (int)magY, (int)((magY - (int)magY) * 100),
                    (int)magZ, (int)((magZ - (int)magZ) * 100));
        }
        if (imu.sample()) {
            float ax = imu.getAccelX();
            float ay = imu.getAccelY();
            float az = imu.getAccelZ();
            float gx = imu.getGyroX();
            float gy = imu.getGyroY();
            float gz = imu.getGyroZ();
            
            // Handle negative values correctly
            int ax_int = (int)(ax * 100);
            int ay_int = (int)(ay * 100);
            int az_int = (int)(az * 100);
            int gx_int = (int)(gx * 100);
            int gy_int = (int)(gy * 100);
            int gz_int = (int)(gz * 100);
            
            LOG_INF("Accel [mg]: X=%d.%02d, Y=%d.%02d, Z=%d.%02d",
                    ax_int / 100, abs(ax_int % 100),
                    ay_int / 100, abs(ay_int % 100),
                    az_int / 100, abs(az_int % 100));
            LOG_INF("Gyro [mdps]: X=%d.%02d, Y=%d.%02d, Z=%d.%02d",
                    gx_int / 100, abs(gx_int % 100),
                    gy_int / 100, abs(gy_int % 100),
                    gz_int / 100, abs(gz_int % 100));
        }

        if(lps.sample()) {
            float pressure = lps.getPressure();
            float temperature = lps.getTemperature();
            LOG_INF("Pressure: %d.%02d kPa, Temperature: %d.%02d °C",
                    (int)pressure, (int)((pressure - (int)pressure) * 100),
                    (int)temperature, (int)((temperature - (int)temperature) * 100));
        }

        if(tof.isReady() && tof.sample()) {
            float distance = tof.getDistance();
            LOG_INF("Distance: %d.%02d mm",
                    (int)distance, (int)((distance - (int)distance) * 100));
        }

        if(als.sample()) {
            float lux = als.getLux();
            LOG_INF("Ambient Light: %d.%02d lux",
                    (int)lux, (int)((lux - (int)lux) * 100));
        }
        LOG_INF("-------------------------------------------------------------------");
        
        k_sleep(K_SECONDS(2));
    }
}
