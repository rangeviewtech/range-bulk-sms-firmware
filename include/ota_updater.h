// ============================================================================
// ota_updater.h — Over-the-air firmware update support
// ============================================================================
#pragma once

#include <Arduino.h>

namespace OtaUpdater {
    // Initialize ArduinoOTA with hostname
    void begin(const String &hostname);

    // Must be called from loop()
    void handle();

    // Check if OTA is currently in progress
    bool isUpdating();
}
