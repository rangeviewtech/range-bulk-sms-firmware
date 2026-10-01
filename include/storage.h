// ============================================================================
// storage.h — LittleFS persistence for config, queue, SIM status, and stats
// ============================================================================
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ─── Device Configuration (persisted in /config.json) ───────────────────────
struct DeviceConfig {
    String apiBase;       // Backend API base URL
    String gatewayId;     // Assigned gateway UUID
    String authToken;     // Bearer token (gt_xxxx)
    String wifiSsid;      // WiFi SSID
    String wifiPassword;  // WiFi password
    bool   paired;        // True if registration completed
};

// ─── Cumulative Statistics (persisted in /stats.json) ────────────────────────
struct DeviceStats {
    uint32_t totalSent;
    uint32_t totalDelivered;
    uint32_t totalFailed;
    uint32_t totalIncoming;
    uint32_t uptimeSeconds;
    uint32_t bootCount;
};

// ─── Queued SMS Job (persisted in /queue.json) ──────────────────────────────
struct SmsJob {
    String attemptId;
    String messageId;
    String recipientId;
    String phone;
    String message;
    String expiresAt;    // ISO8601 timestamp
    String encoding;     // "GSM7" or "UCS2"
    int8_t assignedSim;  // -1 = unassigned
    uint8_t retryCount;
};

namespace Storage {
    // Initialize LittleFS
    bool begin();

    // ─── Config ─────────────────────────────────────────────────────────
    bool loadConfig(DeviceConfig &cfg);
    bool saveConfig(const DeviceConfig &cfg);
    bool clearConfig();

    // ─── Stats ──────────────────────────────────────────────────────────
    bool loadStats(DeviceStats &stats);
    bool saveStats(const DeviceStats &stats);

    // ─── SMS Queue ──────────────────────────────────────────────────────
    bool loadQueue(std::vector<SmsJob> &jobs);
    bool saveQueue(const std::vector<SmsJob> &jobs);
    bool clearQueue();

    // ─── SIM Status ─────────────────────────────────────────────────────
    // Per-SIM health is managed by SimManager and persisted here
    bool loadSimStatus(JsonDocument &doc);
    bool saveSimStatus(const JsonDocument &doc);

    // ─── Utility ────────────────────────────────────────────────────────
    bool formatFS();
    size_t getFreeSpace();
    void factoryReset();  // Delete all config/state files (does NOT restart)
}
