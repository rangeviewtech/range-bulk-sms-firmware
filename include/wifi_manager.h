// ============================================================================
// wifi_manager.h — WiFi connection + captive portal provisioning
// ============================================================================
#pragma once

#include <Arduino.h>
#include "storage.h"

namespace WifiMgr {
    // Initialize WiFi. If config has credentials, tries to connect.
    // If no credentials or connection fails, starts AP with captive portal.
    // Returns true if connected to WiFi station.
    bool begin(DeviceConfig &cfg);

    // Check if currently connected to WiFi
    bool isConnected();

    // Reconnect to WiFi (with exponential backoff)
    bool reconnect();

    // Start captive portal for provisioning
    // Blocks until user submits credentials and pairing code
    // Returns the pairing code entered by user
    String startPortal(DeviceConfig &cfg);

    // Get device's unique suffix (last 4 hex of MAC)
    String getDeviceSuffix();

    // Get IP address
    String getIP();

    // Update loop (check for disconnects)
    void update();
}
