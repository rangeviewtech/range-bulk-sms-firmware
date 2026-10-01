// power_monitor.h
#pragma once
#include <Arduino.h>

enum class PowerSource : uint8_t {
    UNKNOWN,
    BATTERY,
    USB,
    DC_ADAPTER,
    POE
};

namespace PowerMonitor {
    void begin();
    void update();  // Call from loop
    
    PowerSource getSource();
    const char* getSourceString();
    float getBatteryVoltage();      // Volts (e.g., 3.7)
    int   getBatteryPercent();      // 0-100, -1 if no battery
    bool  isCharging();
    bool  isLowBattery();           // < 20%
    float getCpuTemperature();      // Celsius (ESP32 internal sensor)
    String getResetReason();        // POWERON, SW_RESET, BROWNOUT, etc.
}
