#include "power_monitor.h"
#include "config.h"
#include <esp_system.h>

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

namespace PowerMonitor {

    static PowerSource currentSource = PowerSource::UNKNOWN;
    static float batteryVoltage = 0.0f;
    static int batteryPercent = -1;
    static bool charging = false;
    static unsigned long lastUpdate = 0;

    void begin() {
        pinMode(BATTERY_ADC_PIN, INPUT);
        pinMode(USB_DETECT_PIN, INPUT);
        update();
    }

    void update() {
        if (millis() - lastUpdate < 2000 && lastUpdate != 0) {
            return;
        }
        lastUpdate = millis();

        // Check USB
        bool usbPresent = digitalRead(USB_DETECT_PIN) == HIGH;
        currentSource = usbPresent ? PowerSource::USB : PowerSource::BATTERY;
        charging = usbPresent; // Basic assumption

        // Read battery voltage
        int adcValue = analogRead(BATTERY_ADC_PIN);
        // ADC to voltage (ESP32 ADC is roughly 0-4095 for 0-3.3V, but non-linear)
        float pinVoltage = (adcValue / 4095.0f) * 3.3f;
        // Voltage divider
        batteryVoltage = pinVoltage * ((BATTERY_R1 + BATTERY_R2) / BATTERY_R2);

        // Rough calibration if needed, simple mapping here
        if (batteryVoltage > 4.2f) batteryVoltage = 4.2f;

        if (batteryVoltage < 2.5f) {
            // Probably no battery
            batteryPercent = -1;
        } else {
            batteryPercent = (int)(((batteryVoltage - 3.0f) / (4.2f - 3.0f)) * 100.0f);
            if (batteryPercent < 0) batteryPercent = 0;
            if (batteryPercent > 100) batteryPercent = 100;
        }
    }

    PowerSource getSource() {
        return currentSource;
    }

    const char* getSourceString() {
        switch (currentSource) {
            case PowerSource::BATTERY: return "BATTERY";
            case PowerSource::USB: return "USB";
            case PowerSource::DC_ADAPTER: return "DC_ADAPTER";
            case PowerSource::POE: return "POE";
            default: return "UNKNOWN";
        }
    }

    float getBatteryVoltage() {
        return batteryVoltage;
    }

    int getBatteryPercent() {
        return batteryPercent;
    }

    bool isCharging() {
        return charging;
    }

    bool isLowBattery() {
        return batteryPercent >= 0 && batteryPercent < 20;
    }

    float getCpuTemperature() {
        return temperatureRead();
    }

    String getResetReason() {
        esp_reset_reason_t reason = esp_reset_reason();
        switch (reason) {
            case ESP_RST_POWERON: return "POWERON";
            case ESP_RST_EXT: return "EXT";
            case ESP_RST_SW: return "SW_RESET";
            case ESP_RST_PANIC: return "PANIC";
            case ESP_RST_INT_WDT: return "INT_WDT";
            case ESP_RST_TASK_WDT: return "TASK_WDT";
            case ESP_RST_WDT: return "WDT";
            case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
            case ESP_RST_BROWNOUT: return "BROWNOUT";
            case ESP_RST_SDIO: return "SDIO";
            default: return "UNKNOWN";
        }
    }

}
