// ============================================================================
// led_status.cpp — LED indicator implementation
// ============================================================================

#include "led_status.h"
#include "config.h"

static LedPattern _currentPattern = LedPattern::OFF;
static uint32_t   _lastToggle = 0;
static bool       _ledState = false;
static uint8_t    _blinkPhase = 0; // For multi-blink patterns

void LedStatus::begin() {
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    _currentPattern = LedPattern::OFF;
}

void LedStatus::setPattern(LedPattern pattern) {
    if (_currentPattern != pattern) {
        _currentPattern = pattern;
        _lastToggle = millis();
        _blinkPhase = 0;
        _ledState = false;
    }
}

LedPattern LedStatus::getPattern() {
    return _currentPattern;
}

void LedStatus::update() {
    uint32_t now = millis();
    uint32_t elapsed = now - _lastToggle;

    switch (_currentPattern) {
        case LedPattern::OFF:
            digitalWrite(STATUS_LED_PIN, LOW);
            break;

        case LedPattern::WIFI_CONNECTING:
            // Fast blink: 100ms on/off
            if (elapsed >= 100) {
                _ledState = !_ledState;
                digitalWrite(STATUS_LED_PIN, _ledState);
                _lastToggle = now;
            }
            break;

        case LedPattern::WIFI_CONNECTED:
            // Slow blink: 1000ms on/off
            if (elapsed >= 1000) {
                _ledState = !_ledState;
                digitalWrite(STATUS_LED_PIN, _ledState);
                _lastToggle = now;
            }
            break;

        case LedPattern::ONLINE_READY:
            // Solid on
            digitalWrite(STATUS_LED_PIN, HIGH);
            break;

        case LedPattern::SENDING_SMS:
            // Double blink: on-off-on-off-pause
            if (_blinkPhase < 4) {
                if (elapsed >= 100) {
                    _ledState = !_ledState;
                    digitalWrite(STATUS_LED_PIN, _ledState);
                    _lastToggle = now;
                    _blinkPhase++;
                }
            } else {
                if (elapsed >= 500) {
                    _blinkPhase = 0;
                    _lastToggle = now;
                }
            }
            break;

        case LedPattern::ERROR_STATE:
            // Very fast blink: 50ms
            if (elapsed >= 50) {
                _ledState = !_ledState;
                digitalWrite(STATUS_LED_PIN, _ledState);
                _lastToggle = now;
            }
            break;

        case LedPattern::AP_MODE:
            // Triple blink: on-off-on-off-on-off-pause
            if (_blinkPhase < 6) {
                if (elapsed >= 150) {
                    _ledState = !_ledState;
                    digitalWrite(STATUS_LED_PIN, _ledState);
                    _lastToggle = now;
                    _blinkPhase++;
                }
            } else {
                if (elapsed >= 800) {
                    _blinkPhase = 0;
                    _lastToggle = now;
                }
            }
            break;

        case LedPattern::OTA_UPDATE:
            // Breathing effect using PWM-like toggle
            {
                // Simple sine-wave approximation with varying on/off times
                uint32_t cycle = (now / 10) % 200; // 2-second cycle
                bool on = (cycle < 100) ? (cycle % 10 < (cycle / 10)) : ((200 - cycle) % 10 < ((200 - cycle) / 10));
                digitalWrite(STATUS_LED_PIN, on);
            }
            break;
    }
}
