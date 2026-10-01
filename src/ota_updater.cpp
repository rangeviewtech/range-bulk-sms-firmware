// ============================================================================
// ota_updater.cpp — ArduinoOTA implementation
// ============================================================================

#include "ota_updater.h"
#include "config.h"
#include "led_status.h"
#include <ArduinoOTA.h>

static const char *TAG = "OTA";
static bool _updating = false;

void OtaUpdater::begin(const String &hostname) {
    ArduinoOTA.setHostname(hostname.c_str());

    ArduinoOTA.onStart([]() {
        _updating = true;
        LedStatus::setPattern(LedPattern::OTA_UPDATE);
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        LOG_I(TAG, "OTA Start: updating %s", type.c_str());
    });

    ArduinoOTA.onEnd([]() {
        _updating = false;
        LOG_I(TAG, "OTA Complete! Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPct = 255;
        uint8_t pct = (progress / (total / 100));
        if (pct != lastPct && pct % 10 == 0) {
            LOG_I(TAG, "OTA Progress: %u%%", pct);
            lastPct = pct;
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        _updating = false;
        LedStatus::setPattern(LedPattern::ERROR_STATE);
        const char *msg = "Unknown";
        switch (error) {
            case OTA_AUTH_ERROR:    msg = "Auth Failed"; break;
            case OTA_BEGIN_ERROR:   msg = "Begin Failed"; break;
            case OTA_CONNECT_ERROR: msg = "Connect Failed"; break;
            case OTA_RECEIVE_ERROR: msg = "Receive Failed"; break;
            case OTA_END_ERROR:     msg = "End Failed"; break;
        }
        LOG_E(TAG, "OTA Error: %s", msg);
    });

    ArduinoOTA.begin();
    LOG_I(TAG, "OTA ready. Hostname: %s", hostname.c_str());
}

void OtaUpdater::handle() {
    ArduinoOTA.handle();
}

bool OtaUpdater::isUpdating() {
    return _updating;
}
