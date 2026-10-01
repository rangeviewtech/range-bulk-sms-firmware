// ============================================================================
// led_status.h — Visual status indicators via built-in LED
// ============================================================================
#pragma once

#include <Arduino.h>

// LED patterns representing device state
enum class LedPattern : uint8_t {
    OFF,             // LED off
    WIFI_CONNECTING, // Fast blink (100ms on/100ms off)
    WIFI_CONNECTED,  // Slow blink (1000ms on/1000ms off) — not paired
    ONLINE_READY,    // Solid on — paired and ready
    SENDING_SMS,     // Double blink
    ERROR_STATE,     // Very fast blink (50ms)
    AP_MODE,         // Triple blink — captive portal active
    OTA_UPDATE       // Breathing effect
};

namespace LedStatus {
    // Initialize LED pin
    void begin();

    // Set current pattern
    void setPattern(LedPattern pattern);

    // Get current pattern
    LedPattern getPattern();

    // Must be called from loop() to update LED state
    void update();
}
