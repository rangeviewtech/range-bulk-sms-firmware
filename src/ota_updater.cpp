// ============================================================================
// ota_updater.cpp — ArduinoOTA and Internet FOTA implementation
// ============================================================================

#include "ota_updater.h"
#include "config.h"
#include "led_status.h"
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

static const char *TAG = "OTA";
static bool _updating = false;

namespace OtaUpdater {

void begin(const String &hostname) {
    ArduinoOTA.setHostname(hostname.c_str());

    ArduinoOTA.onStart([]() {
        _updating = true;
        LedStatus::setPattern(LedPattern::OTA_UPDATE);
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        LOG_I(TAG, "Local OTA Start: updating %s", type.c_str());
    });

    ArduinoOTA.onEnd([]() {
        _updating = false;
        LOG_I(TAG, "Local OTA Complete! Rebooting...");
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
        LOG_E(TAG, "Local OTA Error: %s", msg);
    });

    ArduinoOTA.begin();
    LOG_I(TAG, "Local OTA ready. Hostname: %s", hostname.c_str());
}

void handle() {
    ArduinoOTA.handle();
}

bool isUpdating() {
    return _updating;
}

bool checkAndPerformWebUpdate(const String &apiBase, const String &token) {
    if (apiBase.isEmpty() || token.isEmpty()) return false;

    String url = apiBase + API_FIRMWARE_CHECK;
    LOG_I(TAG, "Checking for Internet FOTA at: %s", url.c_str());

    HTTPClient http;
    http.begin(url);
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Content-Type", "application/json");

    String requestPayload = String("{\"currentVersion\":\"") + FW_VERSION + "\",\"hardwareModel\":\"" + FW_HARDWARE_MODEL + "\"}";
    
    int httpCode = http.POST(requestPayload);
    if (httpCode != 200) {
        LOG_W(TAG, "FOTA check failed. HTTP %d", httpCode);
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
        LOG_E(TAG, "FOTA JSON parse failed: %s", err.c_str());
        return false;
    }

    bool updateAvailable = doc["updateAvailable"] | false;
    if (!updateAvailable) {
        LOG_I(TAG, "No FOTA update available (current: %s)", FW_VERSION);
        return false;
    }

    String newVersion = doc["version"] | "";
    String downloadUrl = doc["downloadUrl"] | "";
    if (downloadUrl.isEmpty()) {
        LOG_E(TAG, "FOTA update available but no download URL provided.");
        return false;
    }

    LOG_I(TAG, "FOTA Update found! Version: %s. Starting download...", newVersion.c_str());
    _updating = true;
    LedStatus::setPattern(LedPattern::OTA_UPDATE);

    // Disable watchdog temporarily for OTA
    disableCore0WDT();
    disableCore1WDT();

    // Prepare HTTPClient for download
    HTTPClient downloadHttp;
    downloadHttp.begin(downloadUrl);
    // Optional: add auth header if the binary requires it
    downloadHttp.addHeader("Authorization", "Bearer " + token);

    t_httpUpdate_return ret = httpUpdate.update(downloadHttp);

    enableCore0WDT();
    enableCore1WDT();
    _updating = false;

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            LOG_E(TAG, "FOTA Update failed: (%d): %s", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            LedStatus::setPattern(LedPattern::ERROR_STATE);
            return false;
        case HTTP_UPDATE_NO_UPDATES:
            LOG_I(TAG, "FOTA Update returned no updates.");
            return false;
        case HTTP_UPDATE_OK:
            LOG_I(TAG, "FOTA Update successful! Rebooting...");
            // System will reboot automatically
            return true;
    }

    return false;
}

} // namespace OtaUpdater
