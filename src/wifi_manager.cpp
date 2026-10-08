// ============================================================================
// wifi_manager.cpp — WiFi connection + captive portal provisioning
// ============================================================================

#include "wifi_manager.h"
#include "config.h"
#include "led_status.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <time.h>

static const char *TAG = "WIFI";
static uint32_t _reconnectDelay = WIFI_RECONNECT_MIN_MS;
static uint32_t _lastReconnectAttempt = 0;
static String   _deviceSuffix;
static String   _pairingCode; // Captured from portal

// Custom parameter for pairing code and API URL
static WiFiManagerParameter *_paramPairingCode = nullptr;
static WiFiManagerParameter *_paramApiUrl = nullptr;
static WiFiManagerParameter *_paramApiRootCa = nullptr;

static bool syncSystemClock() {
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    for (uint8_t attempt = 0; attempt < 20; attempt++) {
        if (time(nullptr) > 1'700'000'000) return true;
        delay(500);
    }
    LOG_W(TAG, "NTP time sync failed; verified TLS connections may be unavailable");
    return false;
}

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
                MDNS.addService("http", "tcp", 80);
                LOG_I(TAG, "mDNS: %s.local", hostname.c_str());
            }

            _reconnectDelay = WIFI_RECONNECT_MIN_MS;
            if (!cfg.apiBase.isEmpty() && cfg.apiBase.startsWith("https://") &&
                cfg.apiRootCa.startsWith("-----BEGIN CERTIFICATE-----") &&
                cfg.apiRootCa.endsWith("-----END CERTIFICATE-----")) {
                syncSystemClock();
                return true;
            }
#if ALLOW_INSECURE_HTTP_DEV
            if (!cfg.apiBase.isEmpty() && cfg.apiBase.startsWith("http://")) {
                syncSystemClock();
                return true;
            }
#endif
            LOG_W(TAG, "Verified backend URL or trusted root CA is missing; opening setup portal");
        }

        LOG_W(TAG, "Failed to connect to saved WiFi");
    }

    // No saved credentials or failed — start portal
    LOG_I(TAG, "Starting captive portal...");
    startPortal(cfg);

    if (WiFi.status() == WL_CONNECTED) syncSystemClock();

    return WiFi.status() == WL_CONNECTED;
}

String WifiMgr::startPortal(DeviceConfig &cfg) {
    LedStatus::setPattern(LedPattern::AP_MODE);

    WiFiManager wm;
    wm.setConfigPortalTimeout(300); // 5 minute timeout

    // Add custom parameters
    _paramApiUrl = new WiFiManagerParameter("apiurl", "API Server URL", cfg.apiBase.c_str(), 128);
    _paramApiRootCa = new WiFiManagerParameter("apica", "Backend Root CA PEM (single line; use \\n for line breaks)", cfg.apiRootCa.c_str(), 2048);
    _paramPairingCode = new WiFiManagerParameter("pairing", "Pairing Code", "", 32);
    wm.addParameter(_paramApiUrl);
    wm.addParameter(_paramApiRootCa);
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
        cfg.apiBase.trim();
        while (cfg.apiBase.endsWith("/")) cfg.apiBase.remove(cfg.apiBase.length() - 1);
        if (!cfg.apiBase.isEmpty() && !cfg.apiBase.endsWith("/api/v1")) {
            cfg.apiBase += "/api/v1";
        }
        if (!cfg.apiBase.startsWith("https://") && !cfg.apiBase.startsWith("http://")) {
            LOG_E(TAG, "Backend URL must start with https://");
            cfg.apiBase = "";
        }

        cfg.apiRootCa = String(_paramApiRootCa->getValue());
        cfg.apiRootCa.trim();
        cfg.apiRootCa.replace("\\n", "\n");
        if (cfg.apiBase.startsWith("https://") &&
            (!cfg.apiRootCa.startsWith("-----BEGIN CERTIFICATE-----") ||
             !cfg.apiRootCa.endsWith("-----END CERTIFICATE-----"))) {
            LOG_E(TAG, "HTTPS backend requires its trusted root CA certificate; secure API calls will fail closed");
            cfg.apiBase = "";
        }
#if !ALLOW_INSECURE_HTTP_DEV
        if (cfg.apiBase.startsWith("http://")) {
            LOG_E(TAG, "Plain HTTP is disabled; configure an HTTPS backend and trusted root CA");
            cfg.apiBase = "";
        }
#endif

        _pairingCode = String(_paramPairingCode->getValue());
        _pairingCode.trim();
        cfg.pairingCode = _pairingCode;

        LOG_I(TAG, "Portal complete. WiFi configured; backend URL %s",
              cfg.apiBase.isEmpty() ? "is missing or invalid" : "configured");

        // Start mDNS
        String hostname = String(OTA_HOSTNAME_PREFIX) + getDeviceSuffix();
        if (MDNS.begin(hostname.c_str())) {
            MDNS.addService("http", "tcp", 80);
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
    delete _paramApiRootCa;
    _paramApiUrl = nullptr;
    _paramPairingCode = nullptr;
    _paramApiRootCa = nullptr;

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
