// ============================================================================
// ota_updater.h — Over-the-air firmware update support
// Supports both Local Network (ArduinoOTA) and Internet (HTTP FOTA) updates
// ============================================================================
#pragma once

#include <Arduino.h>

namespace OtaUpdater {
    // Initialize ArduinoOTA with hostname for local network updates
    void begin(const String &hostname);

    // Must be called from loop() for local OTA updates
    void handle();

    // Check if OTA is currently in progress
    bool isUpdating();

    // Perform an Internet FOTA update check
    // Returns true if an update was triggered and system is restarting
    bool checkAndPerformWebUpdate(const String &apiBase, const String &token);
}
