// ============================================================================
// main.cpp — ESP32 SMS Gateway Firmware Entry Point
// Range View Technologies
//
// This firmware transforms an ESP32 into a multi-SIM SMS gateway appliance.
// It connects to a backend server via WiFi, claims SMS jobs from a queue,
// sends them through connected GSM modems, and reports delivery status.
//
// Boot sequence:
//   1. Initialize LittleFS → load config
//   2. Connect WiFi (or start captive portal for first-time setup)
//   3. If not paired, register with backend using pairing code
//   4. Detect and initialize SIM modules
//   5. Start main sync loop
// ============================================================================

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <ArduinoJson.h>

#include "config.h"
#include "storage.h"
#include "led_status.h"
#include "wifi_manager.h"
#include "sim_manager.h"
#include "sms_engine.h"
#include "api_client.h"
#include "ota_updater.h"

#include "power_monitor.h"
#include "web_dashboard.h"
#include "ussd_handler.h"

static const char *TAG = "MAIN";

// ─── Global State ───────────────────────────────────────────────────────────
static DeviceConfig g_config;
static DeviceStats  g_stats;
static SimManager   g_simMgr;
static SmsEngine   *g_smsEngine = nullptr;

// Timing trackers
static uint32_t g_lastHeartbeat = 0;
static uint32_t g_lastQueuePoll = 0;
static uint32_t g_lastStatsSave = 0;
static uint32_t g_lastSimStatusSave = 0;
static uint32_t g_bootTime = 0;

// ─── Forward Declarations ───────────────────────────────────────────────────
static void doRegistration();
static void syncLoop();
static void printBanner();
static void printStatus();

// ============================================================================
// setup()
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    printBanner();

    g_bootTime = millis();

    // Check factory reset button
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    if (digitalRead(FACTORY_RESET_PIN) == LOW) {
        LOG_I(TAG, "BOOT button held. Waiting for factory reset...");
        uint32_t start = millis();
        bool reset = true;
        while (millis() - start < FACTORY_RESET_HOLD_MS) {
            if (digitalRead(FACTORY_RESET_PIN) != LOW) {
                reset = false;
                break;
            }
            delay(100);
        }
        if (reset) {
            LOG_I(TAG, "Factory reset triggered!");
            Storage::factoryReset(); // assuming this exists or similar
            delay(1000);
            ESP.restart();
        }
    }

    // Initialize PowerMonitor
    PowerMonitor::begin();

    // Step 1: Initialize LED
    LedStatus::begin();
    LedStatus::setPattern(LedPattern::WIFI_CONNECTING);

    // Step 2: Initialize LittleFS
    if (!Storage::begin()) {
        LOG_E(TAG, "FATAL: LittleFS failed! Formatting...");
        Storage::formatFS();
        if (!Storage::begin()) {
            LOG_E(TAG, "FATAL: LittleFS still failed after format. Halting.");
            LedStatus::setPattern(LedPattern::ERROR_STATE);
            while (true) { LedStatus::update(); delay(100); }
        }
    }

    // Step 3: Load config
    Storage::loadConfig(g_config);
    Storage::loadStats(g_stats);
    g_stats.bootCount++;
    Storage::saveStats(g_stats);
    LOG_I(TAG, "Boot #%d", g_stats.bootCount);

    // Step 4: Connect WiFi
    if (!WifiMgr::begin(g_config)) {
        LOG_E(TAG, "WiFi failed — will retry in main loop");
    }

    // Step 5: Register if not paired
    if (WifiMgr::isConnected()) {
        Storage::saveConfig(g_config);
        if (!g_config.paired) {
            if (!g_config.apiBase.isEmpty()) {
                doRegistration();
            } else {
                LOG_E(TAG, "Set the web app API URL in the setup portal before pairing");
            }
        }
    }

    // Step 6: Configure API client
    if (g_config.paired) {
        ApiClient::configure(g_config.apiBase, g_config.authToken, g_config.apiRootCa);
    }

    // Step 7: Detect SIM modules
    LOG_I(TAG, "Initializing SIM modules...");
    uint8_t simCount = g_simMgr.begin();

    if (simCount == 0) {
        LOG_E(TAG, "WARNING: No SIM modules detected! Check wiring.");
        LOG_I(TAG, "Gateway will start but cannot send SMS.");
        LOG_I(TAG, "Expected wiring: SIM0 RX=%d TX=%d, SIM1 RX=%d TX=%d",
              SIM0_RX_PIN, SIM0_TX_PIN, SIM1_RX_PIN, SIM1_TX_PIN);
    } else {
        // Restore SIM health state from flash
        JsonDocument simDoc;
        if (Storage::loadSimStatus(simDoc)) {
            g_simMgr.fromJson(simDoc);
        }
    }

    // Init USSD Handler
    UssdHandler::begin(&g_simMgr);

    // Start Web Dashboard
    if (WEB_DASHBOARD_ENABLED) {
        WebDashboard::begin(&g_simMgr, &g_config, &g_stats);
    }

    // Step 8: Initialize SMS engine
    g_smsEngine = new SmsEngine(g_simMgr);
    g_smsEngine->loadState(); // Restore queued jobs from flash

    // Step 9: OTA updates
    String otaHostname = String(OTA_HOSTNAME_PREFIX) + WifiMgr::getDeviceSuffix();
    OtaUpdater::begin(otaHostname);

    // Step 10: Enable watchdog
    esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    // Ready!
    if (g_config.paired) {
        LedStatus::setPattern(LedPattern::ONLINE_READY);
    } else {
        LedStatus::setPattern(LedPattern::WIFI_CONNECTED);
    }

    LOG_I(TAG, "═══════════════════════════════════════════");
    LOG_I(TAG, "  ESP32 SMS Gateway READY");
    LOG_I(TAG, "  SIM Slots: %d", simCount);
    LOG_I(TAG, "  Paired: %s", g_config.paired ? "YES" : "NO");
    LOG_I(TAG, "  IP: %s", WifiMgr::getIP().c_str());
    LOG_I(TAG, "  Free Heap: %d bytes", ESP.getFreeHeap());
    LOG_I(TAG, "═══════════════════════════════════════════");
}

// ============================================================================
// loop()
// ============================================================================
void loop() {
    // Feed watchdog
    esp_task_wdt_reset();

    PowerMonitor::update();
    WebDashboard::handle();
    UssdHandler::update();

    // Update LED
    LedStatus::update();

    // Handle OTA
    OtaUpdater::handle();
    if (OtaUpdater::isUpdating()) return; // Pause everything during OTA

    // Check WiFi
    WifiMgr::update();
    if (!WifiMgr::isConnected()) {
        LedStatus::setPattern(LedPattern::WIFI_CONNECTING);
        delay(1000);
        return;
    }

    // Skip sync if not paired
    if (!g_config.paired) {
        delay(5000);
        return;
    }

    // Main sync loop
    syncLoop();

    // Small delay to prevent tight-loop CPU burn
    delay(50);
}

// ============================================================================
// syncLoop() — Main operational cycle
// ============================================================================
static void syncLoop() {
    uint32_t now = millis();

    // ─── Step 1: Process URCs from all SIM modules ──────────────────────
    SimManager::UrcEvents events = g_simMgr.processAllURCs();

    // Handle delivery reports
    for (const auto &[slot, report] : events.deliveryReports) {
        g_smsEngine->processDeliveryReport(slot, report);
        if (report.delivered) {
            g_stats.totalDelivered++;
        }
    }

    // Handle incoming SMS
    for (const auto &[slot, sms] : events.incomingSms) {
        LOG_I(TAG, "Incoming SMS from %s on SIM %d: %s",
              sms.from.c_str(), slot, sms.message.c_str());
        ApiClient::reportIncoming(sms.from, sms.to, sms.message,
                                  sms.timestamp, slot);
        g_stats.totalIncoming++;
    }

    // ─── Step 2: Heartbeat & FOTA Check ─────────────────────────────────
    if (now - g_lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        g_lastHeartbeat = now;

        ApiResponse hbResp = ApiClient::heartbeat(&g_simMgr, &g_stats);
        if (hbResp.success) {
            // Check for FOTA updates occasionally
            static uint32_t lastFotaCheck = 0;
            if (now - lastFotaCheck >= FOTA_CHECK_INTERVAL_MS || lastFotaCheck == 0) {
                lastFotaCheck = now;
                // Tokens and base url are stored globally
                OtaUpdater::checkAndPerformWebUpdate(g_config.apiBase, g_config.authToken, g_config.apiRootCa);
            }
            LOG_D(TAG, "Heartbeat OK");
        } else {
            LOG_W(TAG, "Heartbeat failed: %s", hbResp.error.c_str());
        }

        // Update uptime
        g_stats.uptimeSeconds += HEARTBEAT_INTERVAL_MS / 1000;

        // Periodic status print
        printStatus();
    }

    // ─── Step 3: Poll queue for new jobs ────────────────────────────────
    uint32_t pollInterval = (g_smsEngine->pendingCount() > 0)
                            ? QUEUE_POLL_BUSY_MS
                            : QUEUE_POLL_IDLE_MS;

    if (now - g_lastQueuePoll >= pollInterval) {
        g_lastQueuePoll = now;

        // Only poll if we have SIMs available
        if (g_simMgr.getSlotCount() > 0) {
            QueueResponse qResp = ApiClient::fetchQueue(MAX_SMS_PER_POLL);
            if (qResp.success && !qResp.messages.empty()) {
                g_smsEngine->enqueueJobs(qResp.messages);
                g_smsEngine->setMaxThroughput(qResp.maxThroughput);
            }
        }
    }

    // ─── Step 4: Process send queue ─────────────────────────────────────
    if (g_smsEngine->pendingCount() > 0 && g_simMgr.getSlotCount() > 0) {
        g_smsEngine->processSendQueue();

        // Update stats
        g_stats.totalSent += g_smsEngine->getSentCount();
        g_stats.totalFailed += g_smsEngine->getFailCount();
    }

    // ─── Step 5: Check DLR timeouts ─────────────────────────────────────
    g_smsEngine->checkTimeouts();

    // ─── Step 6: Update SIM health ──────────────────────────────────────
    g_simMgr.update();

    // ─── Step 7: Persist state periodically ─────────────────────────────
    if (now - g_lastStatsSave >= 60000) { // Every minute
        g_lastStatsSave = now;
        Storage::saveStats(g_stats);
        g_smsEngine->saveState();
    }

    if (now - g_lastSimStatusSave >= 300000) { // Every 5 minutes
        g_lastSimStatusSave = now;
        JsonDocument simDoc;
        g_simMgr.toJson(simDoc);
        Storage::saveSimStatus(simDoc);
    }

    // ─── Step 8: Health checks ──────────────────────────────────────────
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < LOW_HEAP_THRESHOLD) {
        LOG_W(TAG, "LOW HEAP: %d bytes free!", freeHeap);
    }
}

// ============================================================================
// doRegistration() — Pair this device with a backend gateway
// ============================================================================
static void doRegistration() {
    LOG_I(TAG, "Starting device registration...");

    // The pairing code should have been captured from the captive portal.
    // If we already have a config with a pairing code, use that.
    // Otherwise, the user needs to re-run the captive portal.

    // Check if we have a pairing code from the portal
    // WiFiManager stores this in the config.apiBase flow
    // We'll read from a special field or re-prompt via serial

    String pairingCode = g_config.pairingCode;

    if (pairingCode.isEmpty()) {
        // Prompt via Serial
        LOG_I(TAG, "");
        LOG_I(TAG, "╔══════════════════════════════════════════╗");
        LOG_I(TAG, "║  DEVICE REGISTRATION REQUIRED            ║");
        LOG_I(TAG, "║                                          ║");
        LOG_I(TAG, "║  Enter your pairing code via Serial:     ║");
        LOG_I(TAG, "╚══════════════════════════════════════════╝");
        LOG_I(TAG, "");

        // Wait for serial input (with timeout)
        uint32_t start = millis();
        while (millis() - start < 120000) { // 2 minute timeout
            if (Serial.available()) {
                pairingCode = Serial.readStringUntil('\n');
                pairingCode.trim();
                if (!pairingCode.isEmpty()) break;
            }
            LedStatus::update();
            delay(100);
        }
    }

    if (pairingCode.isEmpty()) {
        LOG_E(TAG, "No pairing code provided. Restart to try again.");
        return;
    }

    LOG_I(TAG, "Registering gateway with the configured backend...");

    RegisterResponse resp = ApiClient::registerDevice(g_config.apiBase, pairingCode, g_config.apiRootCa);

    if (resp.success) {
        g_config.gatewayId = resp.gatewayId;
        g_config.authToken = resp.token;
        g_config.paired = true;
        g_config.pairingCode = "";

        // Save to flash
        Storage::saveConfig(g_config);

        // Configure API client with new token
        ApiClient::configure(g_config.apiBase, g_config.authToken, g_config.apiRootCa);

        LOG_I(TAG, "╔══════════════════════════════════════════╗");
        LOG_I(TAG, "║  REGISTRATION SUCCESSFUL!                ║");
        LOG_I(TAG, "║  Gateway ID: %s", resp.gatewayId.substring(0, 8).c_str());
        LOG_I(TAG, "╚══════════════════════════════════════════╝");

        LedStatus::setPattern(LedPattern::ONLINE_READY);
    } else {
        LOG_E(TAG, "Registration FAILED: %s", resp.error.c_str());
        LOG_E(TAG, "Check pairing code and try again.");
        LedStatus::setPattern(LedPattern::ERROR_STATE);
    }
}

// ============================================================================
// Utility
// ============================================================================

static void printBanner() {
    Serial.println();
    Serial.println("╔══════════════════════════════════════════════════════╗");
    Serial.println("║                                                      ║");
    Serial.println("║   ██████╗  █████╗ ███╗   ██╗ ██████╗ ███████╗       ║");
    Serial.println("║   ██╔══██╗██╔══██╗████╗  ██║██╔════╝ ██╔════╝       ║");
    Serial.println("║   ██████╔╝███████║██╔██╗ ██║██║  ███╗█████╗         ║");
    Serial.println("║   ██╔══██╗██╔══██║██║╚██╗██║██║   ██║██╔══╝         ║");
    Serial.println("║   ██║  ██║██║  ██║██║ ╚████║╚██████╔╝███████╗       ║");
    Serial.println("║   ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝ ╚═════╝ ╚══════╝       ║");
    Serial.println("║                                                      ║");
    Serial.println("║   Range View Technologies                            ║");
    Serial.println("║   ESP32 SMS Gateway Firmware v" FW_VERSION "                  ║");
    Serial.println("║                                                      ║");
    Serial.println("╚══════════════════════════════════════════════════════╝");
    Serial.println();
}

static void printStatus() {
    LOG_I(TAG, "────────── STATUS ──────────");
    LOG_I(TAG, "Power: %s, %.2fV", PowerMonitor::getSourceString(), PowerMonitor::getBatteryVoltage());
    LOG_I(TAG, "Temp: %.1fC", PowerMonitor::getCpuTemperature());
    LOG_I(TAG, "WiFi: %s (%d dBm)", WiFi.SSID().c_str(), WiFi.RSSI());
    LOG_I(TAG, "Uptime: %d min", (millis() - g_bootTime) / 60000);
    LOG_I(TAG, "Free Heap: %d bytes", ESP.getFreeHeap());
    LOG_I(TAG, "SIMs: %d active", g_simMgr.getSlotCount());

    for (uint8_t i = 0; i < g_simMgr.getSlotCount(); i++) {
        const SimSlotState *s = g_simMgr.getSlot(i);
        if (!s) continue;

        const char *healthStr = "?";
        switch (s->health) {
            case SimHealth::HEALTHY:     healthStr = "[OK]"; break;
            case SimHealth::DEGRADED:    healthStr = "[WARN]"; break;
            case SimHealth::BUSY:        healthStr = "[BUSY]"; break;
            case SimHealth::DEAD:        healthStr = "[DEAD]"; break;
            case SimHealth::BLACKLISTED: healthStr = "[BLOCKED]"; break;
            default: healthStr = "[?]"; break;
        }
        
        String bars = "";
        for (uint8_t b=0; b<s->info.signalBars; b++) bars += "█";
        for (uint8_t b=s->info.signalBars; b<5; b++) bars += "▂";

        LOG_I(TAG, "  SIM%d: %s %s | %s | %s | sent=%d fail=%d",
              i, healthStr, s->info.operatorName.c_str(),
              s->info.networkType.c_str(), bars.c_str(), s->totalSent, s->totalFailed);
    }

    LOG_I(TAG, "Queue: %d pending", g_smsEngine ? g_smsEngine->pendingCount() : 0);
    LOG_I(TAG, "Stats: sent=%d delivered=%d failed=%d incoming=%d",
          g_stats.totalSent, g_stats.totalDelivered,
          g_stats.totalFailed, g_stats.totalIncoming);
    LOG_I(TAG, "────────────────────────────");
}
