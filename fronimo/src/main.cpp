#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>
#include "sensors/hts221.h"
#include "sensors/iis2mdc.h"
#include "sensors/ism330dhcx.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("B-U585I-IOT02A C++ Sensor Example");

    HTS221 hts;
    IIS2MDC mag;
    ISM330DHCX imu;

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
        k_sleep(K_SECONDS(2));
    }
}
