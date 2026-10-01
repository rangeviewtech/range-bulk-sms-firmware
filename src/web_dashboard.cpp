// ============================================================================
// web_dashboard.cpp — V380-style local web dashboard for device management
// Serves a full HTML UI at http://device-ip/ with live status, SIM details,
// and admin actions (restart, factory reset, unflag SIM).
// ============================================================================

#include "web_dashboard.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include "config.h"
#include "power_monitor.h"
#include "wifi_manager.h"
#include <WiFi.h>

static const char *TAG = "WEBUI";

namespace WebDashboard {
    static WebServer server(WEB_DASHBOARD_PORT);
    static SimManager* _simMgr = nullptr;
    static DeviceConfig* _cfg = nullptr;
    static DeviceStats* _stats = nullptr;
    static bool running = false;

    // ─── Helper: Health to color + label ──────────────────────────────────
    static String healthBadge(SimHealth h) {
        switch (h) {
            case SimHealth::HEALTHY:     return "<span style='color:#22c55e;font-weight:700'>● HEALTHY</span>";
            case SimHealth::DEGRADED:    return "<span style='color:#f59e0b;font-weight:700'>● DEGRADED</span>";
            case SimHealth::BUSY:        return "<span style='color:#3b82f6;font-weight:700'>● BUSY</span>";
            case SimHealth::DEAD:        return "<span style='color:#ef4444;font-weight:700'>● DEAD</span>";
            case SimHealth::BLACKLISTED: return "<span style='color:#dc2626;font-weight:700'>⊘ BLACKLISTED</span>";
            default:                     return "<span style='color:#6b7280;font-weight:700'>? UNKNOWN</span>";
        }
    }

    // ─── Helper: Signal bars visual ──────────────────────────────────────
    static String signalBarsVisual(uint8_t bars) {
        String result = "<span style='font-family:monospace;letter-spacing:1px'>";
        for (int i = 1; i <= 5; i++) {
            if (i <= bars) {
                result += "<span style='color:#22c55e'>▊</span>";
            } else {
                result += "<span style='color:#374151'>▊</span>";
            }
        }
        result += "</span>";
        return result;
    }

    // ─── GET / → Full Dashboard ──────────────────────────────────────────
    static void handleRoot() {
        String html = R"html(<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>Range SMS Gateway</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#07163D;color:#e2e8f0;min-height:100vh;padding:16px}
.header{display:flex;align-items:center;justify-content:space-between;margin-bottom:20px;flex-wrap:wrap}
.header h1{color:#FBCA07;font-size:22px}
.header .ver{color:#6b7280;font-size:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px;margin-bottom:16px}
.card{background:#0d2057;border-radius:12px;padding:20px;box-shadow:0 4px 12px rgba(0,0,0,0.3)}
.card h2{color:#FBCA07;font-size:16px;margin-bottom:12px;border-bottom:1px solid #1e3a7a;padding-bottom:8px}
.row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #162550}
.row .label{color:#8899bb;font-size:13px}
.row .value{color:#e2e8f0;font-size:13px;font-weight:600}
table{width:100%;border-collapse:collapse;font-size:13px}
th{background:#162550;color:#FBCA07;padding:10px 8px;text-align:left;font-size:12px;text-transform:uppercase;letter-spacing:0.5px}
td{padding:8px;border-bottom:1px solid #162550}
tr:hover{background:#162550}
.btn{background:#FBCA07;color:#07163D;padding:8px 16px;border:none;border-radius:6px;font-weight:700;cursor:pointer;font-size:13px;transition:all 0.2s}
.btn:hover{background:#e5b800;transform:translateY(-1px)}
.btn-danger{background:#dc2626;color:white}
.btn-danger:hover{background:#b91c1c}
.btn-sm{padding:4px 10px;font-size:11px;border-radius:4px}
.stat-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}
.stat{text-align:center;background:#162550;border-radius:8px;padding:12px 8px}
.stat .num{font-size:24px;font-weight:700;color:#FBCA07}
.stat .lbl{font-size:11px;color:#6b7280;margin-top:4px}
.refresh{font-size:11px;color:#4a5f8f;text-align:center;margin-top:16px}
@media(max-width:600px){.stat-grid{grid-template-columns:repeat(2,1fr)}}
</style>
</head><body>
<div class="header">
<h1>⚡ Range SMS Gateway</h1>
<span class="ver">)html";

        html += String(FW_VERSION) + " | " + String(FW_HARDWARE_MODEL);
        html += R"html(</span></div>)html";

        // ─── System Status Card ──────────────────────────────────────────
        html += "<div class='grid'><div class='card'><h2>⚙ System</h2>";
        html += "<div class='row'><span class='label'>Uptime</span><span class='value'>";
        uint32_t upSec = millis() / 1000;
        html += String(upSec / 3600) + "h " + String((upSec % 3600) / 60) + "m " + String(upSec % 60) + "s</span></div>";
        html += "<div class='row'><span class='label'>Free Heap</span><span class='value'>" + String(ESP.getFreeHeap() / 1024) + " KB</span></div>";
        html += "<div class='row'><span class='label'>CPU Temp</span><span class='value'>" + String(PowerMonitor::getCpuTemperature(), 1) + " °C</span></div>";
        html += "<div class='row'><span class='label'>Reset Reason</span><span class='value'>" + PowerMonitor::getResetReason() + "</span></div>";
        html += "<div class='row'><span class='label'>Paired</span><span class='value'>" + String(_cfg->paired ? "✅ Yes" : "❌ No") + "</span></div>";
        if (_cfg->paired) {
            html += "<div class='row'><span class='label'>Gateway ID</span><span class='value' style='font-size:11px'>" + _cfg->gatewayId.substring(0, 12) + "...</span></div>";
        }
        html += "</div>";

        // ─── Power Card ──────────────────────────────────────────────────
        html += "<div class='card'><h2>🔋 Power</h2>";
        html += "<div class='row'><span class='label'>Source</span><span class='value'>" + String(PowerMonitor::getSourceString()) + "</span></div>";
        html += "<div class='row'><span class='label'>Voltage</span><span class='value'>" + String(PowerMonitor::getBatteryVoltage(), 2) + " V</span></div>";
        int batPct = PowerMonitor::getBatteryPercent();
        if (batPct >= 0) {
            String batColor = batPct > 50 ? "#22c55e" : (batPct > 20 ? "#f59e0b" : "#ef4444");
            html += "<div class='row'><span class='label'>Battery</span><span class='value' style='color:" + batColor + "'>" + String(batPct) + "%</span></div>";
        } else {
            html += "<div class='row'><span class='label'>Battery</span><span class='value'>Not detected</span></div>";
        }
        html += "<div class='row'><span class='label'>Charging</span><span class='value'>" + String(PowerMonitor::isCharging() ? "⚡ Yes" : "No") + "</span></div>";
        html += "</div></div>";

        // ─── Connectivity Card ───────────────────────────────────────────
        html += "<div class='grid'><div class='card'><h2>🌐 Connectivity</h2>";
        html += "<div class='row'><span class='label'>Method</span><span class='value'>WiFi</span></div>";
        html += "<div class='row'><span class='label'>SSID</span><span class='value'>" + WiFi.SSID() + "</span></div>";
        html += "<div class='row'><span class='label'>Signal</span><span class='value'>" + String(WiFi.RSSI()) + " dBm</span></div>";
        html += "<div class='row'><span class='label'>IP Address</span><span class='value'>" + WiFi.localIP().toString() + "</span></div>";
        html += "<div class='row'><span class='label'>MAC</span><span class='value' style='font-size:11px'>" + WiFi.macAddress() + "</span></div>";
        html += "<div class='row'><span class='label'>API Server</span><span class='value' style='font-size:11px'>" + _cfg->apiBase + "</span></div>";
        html += "</div>";

        // ─── Stats Card ─────────────────────────────────────────────────
        html += "<div class='card'><h2>📊 Statistics</h2>";
        html += "<div class='stat-grid'>";
        html += "<div class='stat'><div class='num'>" + String(_stats->totalSent) + "</div><div class='lbl'>Sent</div></div>";
        html += "<div class='stat'><div class='num'>" + String(_stats->totalDelivered) + "</div><div class='lbl'>Delivered</div></div>";
        html += "<div class='stat'><div class='num'>" + String(_stats->totalFailed) + "</div><div class='lbl'>Failed</div></div>";
        html += "<div class='stat'><div class='num'>" + String(_stats->totalIncoming) + "</div><div class='lbl'>Incoming</div></div>";
        html += "</div><div class='row' style='margin-top:12px'><span class='label'>Boot Count</span><span class='value'>" + String(_stats->bootCount) + "</span></div>";
        html += "</div></div>";

        // ─── SIM Cards Table ─────────────────────────────────────────────
        html += "<div class='card'><h2>📡 SIM Cards (" + String(_simMgr ? _simMgr->getSlotCount() : 0) + " detected)</h2>";
        if (_simMgr && _simMgr->getSlotCount() > 0) {
            html += "<div style='overflow-x:auto'><table><tr>";
            html += "<th>Slot</th><th>Operator</th><th>Network</th><th>Signal</th><th>Bars</th>";
            html += "<th>Status</th><th>Health</th><th>IMEI</th><th>Phone</th><th>Balance</th>";
            html += "<th>Sent</th><th>Failed</th><th>Delivered</th><th>Action</th></tr>";

            for (uint8_t i = 0; i < _simMgr->getSlotCount(); i++) {
                const SimSlotState* slot = _simMgr->getSlot(i);
                if (!slot) continue;
                html += "<tr>";
                html += "<td style='font-weight:700;color:#FBCA07'>" + String(i) + "</td>";
                html += "<td style='font-weight:600'>" + slot->info.operatorName + "</td>";
                html += "<td>" + slot->info.networkType + "</td>";
                html += "<td>" + String(slot->info.signalDbm) + " dBm</td>";
                html += "<td>" + signalBarsVisual(slot->info.signalBars) + "</td>";
                html += "<td style='font-size:11px'>" + slot->info.registrationStatus + "</td>";
                html += "<td>" + healthBadge(slot->health) + "</td>";
                html += "<td style='font-size:11px'>" + slot->info.imei + "</td>";
                html += "<td style='font-size:11px'>" + slot->info.phoneNumber + "</td>";
                html += "<td>" + slot->info.ussdBalance + "</td>";
                html += "<td>" + String(slot->totalSent) + "</td>";
                html += "<td>" + String(slot->totalFailed) + "</td>";
                html += "<td>" + String(slot->totalDelivered) + "</td>";
                html += "<td>";
                if (slot->health == SimHealth::BLACKLISTED || slot->health == SimHealth::DEAD) {
                    html += "<button class='btn btn-sm' onclick=\"fetch('/api/unflag-sim?slot=" + String(i) + "',{method:'POST'}).then(()=>location.reload())\">Unflag</button>";
                } else {
                    html += "<span style='color:#4a5f8f'>—</span>";
                }
                html += "</td></tr>";
            }
            html += "</table></div>";
        } else {
            html += "<p style='color:#6b7280;padding:20px;text-align:center'>No SIM modules detected. Check wiring.</p>";
        }
        html += "</div>";

        // ─── Actions ─────────────────────────────────────────────────────
        html += "<div class='card'><h2>🔧 Device Actions</h2>";
        html += "<div style='display:flex;gap:12px;flex-wrap:wrap'>";
        html += "<button class='btn' onclick=\"location.reload()\">🔄 Refresh</button>";
        html += "<button class='btn' onclick=\"if(confirm('Restart the gateway?'))fetch('/api/restart',{method:'POST'})\">⟳ Restart</button>";
        html += "<button class='btn btn-danger' onclick=\"if(confirm('⚠️ This will erase all settings and unpair the device. Are you sure?'))fetch('/api/factory-reset',{method:'POST'})\">🗑 Factory Reset</button>";
        html += "</div></div>";

        html += "<p class='refresh'>Auto-refresh: <a href='/' style='color:#FBCA07'>reload</a> | <a href='/api/status' style='color:#04648C'>JSON API</a></p>";
        html += "<script>setTimeout(()=>location.reload(),30000)</script>"; // Auto-refresh every 30s
        html += "</body></html>";

        server.send(200, "text/html", html);
    }

    // ─── GET /api/status → JSON API ──────────────────────────────────────
    static void handleApiStatus() {
        JsonDocument doc;
        doc["firmware"] = FW_VERSION;
        doc["uptime"] = millis() / 1000;
        doc["freeHeap"] = ESP.getFreeHeap();
        doc["cpuTemp"] = PowerMonitor::getCpuTemperature();
        doc["powerSource"] = PowerMonitor::getSourceString();
        doc["batteryVoltage"] = PowerMonitor::getBatteryVoltage();
        doc["batteryPercent"] = PowerMonitor::getBatteryPercent();
        doc["isCharging"] = PowerMonitor::isCharging();
        doc["wifiSSID"] = WiFi.SSID();
        doc["wifiRSSI"] = WiFi.RSSI();
        doc["ip"] = WiFi.localIP().toString();
        doc["mac"] = WiFi.macAddress();
        doc["paired"] = _cfg->paired;

        if (_simMgr) {
            doc["simCount"] = _simMgr->getSlotCount();
            JsonArray slots = doc["sims"].to<JsonArray>();
            for (uint8_t i = 0; i < _simMgr->getSlotCount(); i++) {
                const SimSlotState* s = _simMgr->getSlot(i);
                if (!s) continue;
                JsonObject obj = slots.add<JsonObject>();
                obj["slot"] = i;
                obj["operator"] = s->info.operatorName;
                obj["network"] = s->info.networkType;
                obj["signalDbm"] = s->info.signalDbm;
                obj["bars"] = s->info.signalBars;
                obj["health"] = (int)s->health;
                obj["imei"] = s->info.imei;
                obj["phone"] = s->info.phoneNumber;
                obj["balance"] = s->info.ussdBalance;
                obj["sent"] = s->totalSent;
                obj["failed"] = s->totalFailed;
                obj["delivered"] = s->totalDelivered;
            }
        }

        if (_stats) {
            doc["totalSent"] = _stats->totalSent;
            doc["totalDelivered"] = _stats->totalDelivered;
            doc["totalFailed"] = _stats->totalFailed;
            doc["totalIncoming"] = _stats->totalIncoming;
        }

        String json;
        serializeJsonPretty(doc, json);
        server.send(200, "application/json", json);
    }

    // ─── POST /api/restart ───────────────────────────────────────────────
    static void handleApiRestart() {
        server.send(200, "application/json", "{\"ok\":true,\"message\":\"Restarting...\"}");
        delay(500);
        ESP.restart();
    }

    // ─── POST /api/factory-reset ─────────────────────────────────────────
    static void handleApiFactoryReset() {
        server.send(200, "application/json", "{\"ok\":true,\"message\":\"Factory resetting...\"}");
        LOG_W(TAG, "FACTORY RESET triggered via web dashboard!");
        Storage::factoryReset();
        delay(500);
        ESP.restart();
    }

    // ─── POST /api/unflag-sim?slot=N ─────────────────────────────────────
    static void handleApiUnflagSim() {
        if (server.hasArg("slot") && _simMgr) {
            uint8_t slot = server.arg("slot").toInt();
            _simMgr->unflag(slot);
            LOG_I(TAG, "SIM slot %d unflagged via web dashboard", slot);
            server.send(200, "application/json", "{\"ok\":true}");
        } else {
            server.send(400, "application/json", "{\"error\":\"Missing slot parameter\"}");
        }
    }

    // ─── Lifecycle ───────────────────────────────────────────────────────
    void begin(SimManager *simMgr, DeviceConfig *cfg, DeviceStats *stats) {
        _simMgr = simMgr;
        _cfg = cfg;
        _stats = stats;

        server.on("/", HTTP_GET, handleRoot);
        server.on("/api/status", HTTP_GET, handleApiStatus);
        server.on("/api/restart", HTTP_POST, handleApiRestart);
        server.on("/api/factory-reset", HTTP_POST, handleApiFactoryReset);
        server.on("/api/unflag-sim", HTTP_POST, handleApiUnflagSim);

        server.begin();
        running = true;
        LOG_I(TAG, "Web dashboard started on port %d", WEB_DASHBOARD_PORT);
    }

    void handle() {
        if (running) {
            server.handleClient();
        }
    }

    void stop() {
        server.stop();
        running = false;
    }

    bool isRunning() {
        return running;
    }
}
