// ============================================================================
// storage.cpp — LittleFS persistence implementation
// ============================================================================

#include "storage.h"
#include "config.h"
#include <LittleFS.h>

static const char *TAG = "STORAGE";

// ─── Helper: Read entire file ───────────────────────────────────────────────
static String readFile(const char *path) {
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
}

// ─── Helper: Write entire file ──────────────────────────────────────────────
static bool writeFile(const char *path, const String &content) {
    File f = LittleFS.open(path, "w");
    if (!f) {
        LOG_E(TAG, "Failed to open %s for writing", path);
        return false;
    }
    size_t written = f.print(content);
    f.close();
    return written == content.length();
}

// ─── Initialize ─────────────────────────────────────────────────────────────
bool Storage::begin() {
    if (!LittleFS.begin(true)) { // true = format on first use
        LOG_E(TAG, "LittleFS mount failed!");
        return false;
    }
    LOG_I(TAG, "LittleFS mounted. Free: %u bytes", getFreeSpace());
    return true;
}

// ─── Config ─────────────────────────────────────────────────────────────────
bool Storage::loadConfig(DeviceConfig &cfg) {
    String raw = readFile(CONFIG_FILE);
    if (raw.isEmpty()) {
        LOG_W(TAG, "No config file found, using defaults");
        cfg.apiBase = DEFAULT_API_BASE;
        cfg.apiRootCa = "";
        cfg.pairingCode = "";
        cfg.paired = false;
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, raw);
    if (err) {
        LOG_E(TAG, "Config parse error: %s", err.c_str());
        cfg.apiBase = DEFAULT_API_BASE;
        cfg.apiRootCa = "";
        cfg.pairingCode = "";
        cfg.paired = false;
        return false;
    }

    cfg.apiBase      = doc["apiBase"]      | DEFAULT_API_BASE;
    cfg.apiRootCa    = doc["apiRootCa"]    | "";
    cfg.gatewayId    = doc["gatewayId"]    | "";
    cfg.authToken    = doc["authToken"]    | "";
    cfg.wifiSsid     = doc["wifiSsid"]    | "";
    cfg.wifiPassword = doc["wifiPassword"] | "";
    cfg.pairingCode  = doc["pairingCode"] | "";
    cfg.paired       = doc["paired"]       | false;

    // Migrate devices off the old development-only private LAN default.
    if (cfg.apiBase == "http://192.168.1.100:3000/api/v1") {
        cfg.apiBase = DEFAULT_API_BASE;
    }

    LOG_I(TAG, "Config loaded. Paired: %s, Gateway: %s",
          cfg.paired ? "yes" : "no", cfg.gatewayId.c_str());
    return true;
}

bool Storage::saveConfig(const DeviceConfig &cfg) {
    JsonDocument doc;
    doc["apiBase"]      = cfg.apiBase;
    doc["apiRootCa"]    = cfg.apiRootCa;
    doc["gatewayId"]    = cfg.gatewayId;
    doc["authToken"]    = cfg.authToken;
    doc["wifiSsid"]     = cfg.wifiSsid;
    doc["wifiPassword"] = cfg.wifiPassword;
    doc["pairingCode"]  = cfg.pairingCode;
    doc["paired"]       = cfg.paired;

    String output;
    serializeJson(doc, output);

    if (!writeFile(CONFIG_FILE, output)) {
        LOG_E(TAG, "Failed to save config");
        return false;
    }
    LOG_I(TAG, "Config saved successfully");
    return true;
}

bool Storage::clearConfig() {
    LittleFS.remove(CONFIG_FILE);
    LOG_I(TAG, "Config cleared");
    return true;
}

// ─── Stats ──────────────────────────────────────────────────────────────────
bool Storage::loadStats(DeviceStats &stats) {
    String raw = readFile(STATS_FILE);
    if (raw.isEmpty()) {
        memset(&stats, 0, sizeof(stats));
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, raw)) {
        memset(&stats, 0, sizeof(stats));
        return false;
    }

    stats.totalSent      = doc["totalSent"]      | 0;
    stats.totalDelivered = doc["totalDelivered"] | 0;
    stats.totalFailed    = doc["totalFailed"]    | 0;
    stats.totalIncoming  = doc["totalIncoming"]  | 0;
    stats.uptimeSeconds  = doc["uptimeSeconds"]  | 0;
    stats.bootCount      = doc["bootCount"]      | 0;
    return true;
}

bool Storage::saveStats(const DeviceStats &stats) {
    JsonDocument doc;
    doc["totalSent"]      = stats.totalSent;
    doc["totalDelivered"] = stats.totalDelivered;
    doc["totalFailed"]    = stats.totalFailed;
    doc["totalIncoming"]  = stats.totalIncoming;
    doc["uptimeSeconds"]  = stats.uptimeSeconds;
    doc["bootCount"]      = stats.bootCount;

    String output;
    serializeJson(doc, output);
    return writeFile(STATS_FILE, output);
}

// ─── SMS Queue ──────────────────────────────────────────────────────────────
bool Storage::loadQueue(std::vector<SmsJob> &jobs) {
    jobs.clear();
    String raw = readFile(QUEUE_FILE);
    if (raw.isEmpty()) return false;

    JsonDocument doc;
    if (deserializeJson(doc, raw)) return false;

    JsonArray arr = doc["jobs"].as<JsonArray>();
    for (JsonObject obj : arr) {
        SmsJob job;
        job.attemptId   = obj["attemptId"]   | "";
        job.messageId   = obj["messageId"]   | "";
        job.recipientId = obj["recipientId"] | "";
        job.phone       = obj["phone"]       | "";
        job.message     = obj["message"]     | "";
        job.expiresAt   = obj["expiresAt"]   | "";
        job.encoding    = obj["encoding"]    | "GSM7";
        job.assignedSim = obj["assignedSim"] | -1;
        job.retryCount  = obj["retryCount"]  | 0;
        jobs.push_back(job);
    }
    LOG_I(TAG, "Loaded %d queued jobs from flash", jobs.size());
    return true;
}

bool Storage::saveQueue(const std::vector<SmsJob> &jobs) {
    JsonDocument doc;
    JsonArray arr = doc["jobs"].to<JsonArray>();

    for (const auto &job : jobs) {
        JsonObject obj = arr.add<JsonObject>();
        obj["attemptId"]   = job.attemptId;
        obj["messageId"]   = job.messageId;
        obj["recipientId"] = job.recipientId;
        obj["phone"]       = job.phone;
        obj["message"]     = job.message;
        obj["expiresAt"]   = job.expiresAt;
        obj["encoding"]    = job.encoding;
        obj["assignedSim"] = job.assignedSim;
        obj["retryCount"]  = job.retryCount;
    }

    String output;
    serializeJson(doc, output);
    return writeFile(QUEUE_FILE, output);
}

bool Storage::clearQueue() {
    LittleFS.remove(QUEUE_FILE);
    return true;
}

// ─── SIM Status ─────────────────────────────────────────────────────────────
bool Storage::loadSimStatus(JsonDocument &doc) {
    String raw = readFile(SIM_STATUS_FILE);
    if (raw.isEmpty()) return false;
    return !deserializeJson(doc, raw);
}

bool Storage::saveSimStatus(const JsonDocument &doc) {
    String output;
    serializeJson(doc, output);
    return writeFile(SIM_STATUS_FILE, output);
}

// ─── Utility ────────────────────────────────────────────────────────────────
bool Storage::formatFS() {
    LOG_W(TAG, "Formatting LittleFS!");
    return LittleFS.format();
}

size_t Storage::getFreeSpace() {
    return LittleFS.totalBytes() - LittleFS.usedBytes();
}

void Storage::factoryReset() {
    LOG_W(TAG, "╔══════════════════════════════════╗");
    LOG_W(TAG, "║       FACTORY RESET              ║");
    LOG_W(TAG, "╚══════════════════════════════════╝");

    // Delete all config/state files
    if (LittleFS.exists(CONFIG_FILE))     LittleFS.remove(CONFIG_FILE);
    if (LittleFS.exists(QUEUE_FILE))      LittleFS.remove(QUEUE_FILE);
    if (LittleFS.exists(SIM_STATUS_FILE)) LittleFS.remove(SIM_STATUS_FILE);
    if (LittleFS.exists(STATS_FILE))      LittleFS.remove(STATS_FILE);

    LOG_W(TAG, "All configuration files deleted. Device will restart unpaired.");
}
