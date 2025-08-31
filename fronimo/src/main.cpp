#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sensors/hts221.h"
#include "sensors/iis2mdc.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

int main(void) {
    LOG_INF("B-U585I-IOT02A C++ Sensor Example");

    HTS221 hts;
    IIS2MDC iis2mdc;

    if (!hts.isReady()) {
        LOG_ERR("HTS221 not available");
        return 0;
    }

    if (!iis2mdc.isReady()) {
        LOG_ERR("IIS2MDC not available");
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
        if (iis2mdc.sample()) {
            float magX = iis2mdc.getMagX();
            float magY = iis2mdc.getMagY();
            float magZ = iis2mdc.getMagZ();
            LOG_INF("Mag [uT]: X=%d.%02d, Y=%d.%02d, Z=%d.%02d",
                    (int)magX, (int)((magX - (int)magX) * 100),
                    (int)magY, (int)((magY - (int)magY) * 100),
                    (int)magZ, (int)((magZ - (int)magZ) * 100));
        }
        k_sleep(K_SECONDS(2));
    }
}
