// ============================================================================
// wifi_manager.cpp — WiFi connection + captive portal provisioning
// ============================================================================

#include "wifi_manager.h"
#include "config.h"
#include "led_status.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>

static const char *TAG = "WIFI";
static uint32_t _reconnectDelay = WIFI_RECONNECT_MIN_MS;
static uint32_t _lastReconnectAttempt = 0;
static String   _deviceSuffix;
static String   _pairingCode; // Captured from portal

// Custom parameter for pairing code and API URL
static WiFiManagerParameter *_paramPairingCode = nullptr;
static WiFiManagerParameter *_paramApiUrl = nullptr;

String WifiMgr::getDeviceSuffix() {
    if (_deviceSuffix.isEmpty()) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char suffix[5];
        snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
        _deviceSuffix = String(suffix);
    }
    return _deviceSuffix;
}

bool WifiMgr::begin(DeviceConfig &cfg) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    String hostname = String(OTA_HOSTNAME_PREFIX) + getDeviceSuffix();
    WiFi.setHostname(hostname.c_str());

    if (!cfg.wifiSsid.isEmpty()) {
        LOG_I(TAG, "Connecting to saved WiFi: %s", cfg.wifiSsid.c_str());
        LedStatus::setPattern(LedPattern::WIFI_CONNECTING);

        WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());

        // Wait up to 15 seconds
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(500);
            attempts++;
            LedStatus::update();
        }

        if (WiFi.status() == WL_CONNECTED) {
            LOG_I(TAG, "Connected! IP: %s", WiFi.localIP().toString().c_str());
            LedStatus::setPattern(LedPattern::WIFI_CONNECTED);

            // Start mDNS
            if (MDNS.begin(hostname.c_str())) {
                LOG_I(TAG, "mDNS: %s.local", hostname.c_str());
            }

            _reconnectDelay = WIFI_RECONNECT_MIN_MS;
            return true;
        }

        LOG_W(TAG, "Failed to connect to saved WiFi");
    }

    // No saved credentials or failed — start portal
    LOG_I(TAG, "Starting captive portal...");
    String portalCode = startPortal(cfg);

    return WiFi.status() == WL_CONNECTED;
}

String WifiMgr::startPortal(DeviceConfig &cfg) {
    LedStatus::setPattern(LedPattern::AP_MODE);

    WiFiManager wm;
    wm.setConfigPortalTimeout(300); // 5 minute timeout

    // Add custom parameters
    _paramApiUrl = new WiFiManagerParameter("apiurl", "API Server URL", cfg.apiBase.c_str(), 128);
    _paramPairingCode = new WiFiManagerParameter("pairing", "Pairing Code", "", 32);
    wm.addParameter(_paramApiUrl);
    wm.addParameter(_paramPairingCode);

    String apName = String(AP_SSID_PREFIX) + getDeviceSuffix();
    LOG_I(TAG, "Portal AP: %s", apName.c_str());

    // This blocks until WiFi is configured or timeout
    bool connected = wm.startConfigPortal(apName.c_str(), AP_PASSWORD);

    if (connected) {
        // Save WiFi credentials
        cfg.wifiSsid = WiFi.SSID();
        cfg.wifiPassword = WiFi.psk();
        cfg.apiBase = String(_paramApiUrl->getValue());
        if (cfg.apiBase.isEmpty()) cfg.apiBase = DEFAULT_API_BASE;

        _pairingCode = String(_paramPairingCode->getValue());

        LOG_I(TAG, "Portal complete. SSID=%s, API=%s, Code=%s",
              cfg.wifiSsid.c_str(), cfg.apiBase.c_str(), _pairingCode.c_str());

        // Start mDNS
        String hostname = String(OTA_HOSTNAME_PREFIX) + getDeviceSuffix();
        if (MDNS.begin(hostname.c_str())) {
            LOG_I(TAG, "mDNS: %s.local", hostname.c_str());
        }

        LedStatus::setPattern(LedPattern::WIFI_CONNECTED);
    } else {
        LOG_W(TAG, "Portal timeout — restarting...");
        LedStatus::setPattern(LedPattern::ERROR_STATE);
        delay(2000);
        ESP.restart();
    }

    // Cleanup
    delete _paramApiUrl;
    delete _paramPairingCode;
    _paramApiUrl = nullptr;
    _paramPairingCode = nullptr;

    return _pairingCode;
}

bool WifiMgr::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool WifiMgr::reconnect() {
    if (WiFi.status() == WL_CONNECTED) return true;

    uint32_t now = millis();
    if (now - _lastReconnectAttempt < _reconnectDelay) return false;

    _lastReconnectAttempt = now;
    LOG_I(TAG, "Reconnecting WiFi... (delay=%d ms)", _reconnectDelay);
    LedStatus::setPattern(LedPattern::WIFI_CONNECTING);

    WiFi.reconnect();
    delay(5000);

    if (WiFi.status() == WL_CONNECTED) {
        LOG_I(TAG, "Reconnected! IP: %s", WiFi.localIP().toString().c_str());
        _reconnectDelay = WIFI_RECONNECT_MIN_MS;
        return true;
    }

    // Exponential backoff
    _reconnectDelay = min(_reconnectDelay * 2, (uint32_t)WIFI_RECONNECT_MAX_MS);
    LOG_W(TAG, "Reconnect failed. Next attempt in %d ms", _reconnectDelay);
    return false;
}

String WifiMgr::getIP() {
    return WiFi.localIP().toString();
}

void WifiMgr::update() {
    if (!isConnected()) {
        reconnect();
    }
}
