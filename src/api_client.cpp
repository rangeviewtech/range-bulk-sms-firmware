// ============================================================================
// api_client.cpp — Backend REST API client implementation
// ============================================================================

#include "api_client.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include "power_monitor.h"
#include "sim_manager.h"
#include "crypto_utils.h"

static const char *TAG = "API";
static String _baseUrl;
static String _authToken;

void ApiClient::configure(const String &baseUrl, const String &token) {
    _baseUrl = baseUrl;
    _authToken = token;
    LOG_I(TAG, "Configured: base=%s, token=%s...%s",
          baseUrl.c_str(),
          token.substring(0, 6).c_str(),
          token.substring(token.length() - 4).c_str());
}

bool ApiClient::isConfigured() {
    return !_baseUrl.isEmpty() && !_authToken.isEmpty();
}

// ─── Helper: Make authenticated HTTP request ────────────────────────────────
static int httpRequest(const String &method, const String &path,
                       const String &body, String &response) {
    if (WiFi.status() != WL_CONNECTED) {
        LOG_E(TAG, "WiFi not connected");
        return -1;
    }

    HTTPClient http;
    String url = _baseUrl + path;

    LOG_D(TAG, "%s %s", method.c_str(), url.c_str());

    if (!http.begin(url)) {
        LOG_E(TAG, "HTTP begin failed");
        return -1;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + _authToken);
    http.setTimeout(15000); // 15 second timeout

    if (method == "GET") {
        http.addHeader("X-E2EE", "true");
    }

    int httpCode;
    if (method == "POST") {
        if (!body.isEmpty() && !_authToken.isEmpty() && _authToken.startsWith("gt_")) {
            String secret = _authToken.substring(3); // strip "gt_"
            String encrypted = CryptoUtils::encryptE2EE(body, secret);
            
            JsonDocument e2eeDoc;
            e2eeDoc["e2ee"] = encrypted;
            String e2eeBody;
            serializeJson(e2eeDoc, e2eeBody);
            httpCode = http.POST(e2eeBody);
        } else {
            httpCode = http.POST(body);
        }
    } else if (method == "GET") {
        httpCode = http.GET();
    } else {
        http.end();
        return -1;
    }

    if (httpCode > 0) {
        String rawResponse = http.getString();
        
        JsonDocument resDoc;
        DeserializationError err = deserializeJson(resDoc, rawResponse);
        if (!err && resDoc.containsKey("e2ee") && !_authToken.isEmpty() && _authToken.startsWith("gt_")) {
            String secret = _authToken.substring(3);
            String decrypted = CryptoUtils::decryptE2EE(resDoc["e2ee"].as<String>(), secret);
            if (!decrypted.isEmpty()) {
                response = decrypted;
            } else {
                response = rawResponse; // Fallback
            }
        } else {
            response = rawResponse;
        }
        
        LOG_D(TAG, "Response [%d]: %s", httpCode,
              response.length() > 200 ? (response.substring(0, 200) + "...").c_str() : response.c_str());
    } else {
        LOG_E(TAG, "HTTP error: %s", http.errorToString(httpCode).c_str());
        response = "";
    }

    http.end();
    return httpCode;
}

// ─── Register (no auth token yet) ───────────────────────────────────────────
RegisterResponse ApiClient::registerDevice(const String &baseUrl, const String &pairingCode) {
    RegisterResponse result = { false, "", "", "" };

    if (WiFi.status() != WL_CONNECTED) {
        result.error = "WiFi not connected";
        return result;
    }

    HTTPClient http;
    String url = baseUrl + API_REGISTER;

    if (!http.begin(url)) {
        result.error = "HTTP begin failed";
        return result;
    }

    http.addHeader("Content-Type", "application/json");
    http.setTimeout(15000);

    JsonDocument doc;
    doc["pairingCode"] = pairingCode;
    doc["hardwareModel"] = FW_HARDWARE_MODEL;
    doc["appVersion"] = FW_VERSION;
    doc["osVersion"] = FW_OS_VERSION;

    String body;
    serializeJson(doc, body);

    LOG_I(TAG, "Registering with pairing code: %s", pairingCode.c_str());
    int httpCode = http.POST(body);

    if (httpCode == 200) {
        String response = http.getString();
        JsonDocument resDoc;
        if (!deserializeJson(resDoc, response)) {
            result.success = resDoc["success"] | false;
            result.gatewayId = resDoc["gatewayId"] | "";
            result.token = resDoc["token"] | "";
            if (result.success) {
                LOG_I(TAG, "Registration successful! GW=%s", result.gatewayId.c_str());
            }
        }
    } else {
        String errBody = http.getString();
        JsonDocument errDoc;
        if (!deserializeJson(errDoc, errBody)) {
            result.error = errDoc["error"] | "Registration failed";
        } else {
            result.error = "HTTP " + String(httpCode);
        }
        LOG_E(TAG, "Registration failed [%d]: %s", httpCode, result.error.c_str());
    }

    http.end();
    return result;
}

// ─── Heartbeat ──────────────────────────────────────────────────────────────
ApiResponse ApiClient::heartbeat(class SimManager* simMgr, struct DeviceStats* stats) {
    ApiResponse result = { false, "", "", 0 };

    JsonDocument doc;
    doc["batteryLevel"] = PowerMonitor::getBatteryPercent();
    doc["isCharging"] = PowerMonitor::isCharging();
    doc["signalStrength"] = simMgr ? simMgr->getBestSignalDbm() : -99;
    doc["networkOperator"] = simMgr ? simMgr->getBestOperator() : "Unknown";
    doc["powerSource"] = PowerMonitor::getSourceString();
    doc["batteryVoltage"] = (int)(PowerMonitor::getBatteryVoltage() * 1000); // mV
    doc["connectivityMethod"] = "WIFI";
    doc["wifiSSID"] = WiFi.SSID();
    doc["wifiRSSI"] = WiFi.RSSI();
    doc["macAddress"] = WiFi.macAddress();
    doc["localIP"] = WiFi.localIP().toString();
    doc["firmwareVersion"] = FW_VERSION;
    doc["freeHeapBytes"] = ESP.getFreeHeap();
    doc["uptimeSeconds"] = stats ? stats->uptimeSeconds : 0;
    doc["cpuTempCelsius"] = PowerMonitor::getCpuTemperature();
    doc["resetReason"] = PowerMonitor::getResetReason();
    
    if (simMgr) {
        doc["simSlotCount"] = simMgr->getSlotCount();
        JsonArray slots = doc["simSlots"].to<JsonArray>();
        for (uint8_t i = 0; i < simMgr->getSlotCount(); i++) {
            const SimSlotState *s = simMgr->getSlot(i);
            if (!s) continue;
            JsonObject slotObj = slots.add<JsonObject>();
            slotObj["slot"] = i;
            
            const char* healthStr = "UNKNOWN";
            switch(s->health) {
                case SimHealth::HEALTHY: healthStr = "HEALTHY"; break;
                case SimHealth::DEGRADED: healthStr = "DEGRADED"; break;
                case SimHealth::BUSY: healthStr = "BUSY"; break;
                case SimHealth::DEAD: healthStr = "DEAD"; break;
                case SimHealth::BLACKLISTED: healthStr = "BLACKLISTED"; break;
                default: break;
            }
            slotObj["health"] = healthStr;
            slotObj["operatorName"] = s->info.operatorName;
            slotObj["signalDbm"] = s->info.signalDbm;
            slotObj["signalBars"] = s->info.signalBars;
            slotObj["networkType"] = s->info.networkType;
            slotObj["registrationStatus"] = s->info.registrationStatus;
            slotObj["imei"] = s->info.imei;
            slotObj["iccid"] = s->info.iccid;
            slotObj["phoneNumber"] = s->info.phoneNumber;
            slotObj["totalSent"] = s->totalSent;
            slotObj["totalFailed"] = s->totalFailed;
            slotObj["totalDelivered"] = s->totalDelivered;
            slotObj["ussdBalance"] = s->info.ussdBalance;
        }
    } else {
        doc["simSlotCount"] = 0;
        doc["simSlots"].to<JsonArray>();
    }
    
    if (stats) {
        doc["totalSmsSent"] = stats->totalSent;
        doc["totalSmsDelivered"] = stats->totalDelivered;
        doc["totalSmsFailed"] = stats->totalFailed;
        doc["totalSmsIncoming"] = stats->totalIncoming;
    }
    doc["pendingQueueSize"] = 0; // Simplified

    String body;
    serializeJson(doc, body);

    String response;
    result.httpCode = httpRequest("POST", API_HEARTBEAT, body, response);

    if (result.httpCode == 200) {
        JsonDocument resDoc;
        if (!deserializeJson(resDoc, response)) {
            result.success = resDoc["success"] | false;
            
            // Handle unflag commands
            JsonArray commands = resDoc["commands"].as<JsonArray>();
            if (simMgr) {
                for (JsonObject cmd : commands) {
                    if (cmd["action"] == "UNFLAG_SIM") {
                        uint8_t slot = cmd["slot"] | 255;
                        if (slot != 255) {
                            simMgr->unflag(slot);
                        }
                    }
                }
            }
        }
    } else {
        result.error = "HTTP " + String(result.httpCode);
    }

    return result;
}

// ─── Fetch Queue ────────────────────────────────────────────────────────────
QueueResponse ApiClient::fetchQueue(int limit) {
    QueueResponse result = { false, {}, 30, "" };

    String path = String(API_QUEUE) + "?limit=" + String(limit);
    String response;
    int httpCode = httpRequest("GET", path, "", response);

    if (httpCode == 200) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, response);
        if (err) {
            result.error = "JSON parse error: " + String(err.c_str());
            return result;
        }

        result.success = doc["success"] | false;
        result.maxThroughput = doc["maxThroughput"] | 30;

        JsonArray msgs = doc["messages"].as<JsonArray>();
        for (JsonObject msg : msgs) {
            QueuedMessage qm;
            qm.attemptId   = msg["attemptId"]   | "";
            qm.messageId   = msg["messageId"]   | "";
            qm.recipientId = msg["recipientId"] | "";
            qm.phone       = msg["phone"]       | "";
            qm.message     = msg["message"]     | "";
            qm.expiresAt   = msg["expiresAt"]   | "";
            qm.encoding    = msg["encoding"]    | "GSM7";
            result.messages.push_back(qm);
        }

        LOG_I(TAG, "Fetched %d messages from queue (throughput=%d/min)",
              result.messages.size(), result.maxThroughput);
    } else {
        result.error = "HTTP " + String(httpCode);
    }

    return result;
}

// ─── Report Result ──────────────────────────────────────────────────────────
ApiResponse ApiClient::reportResult(
    const String &attemptId,
    const String &status,
    bool hasCarrierDlr,
    int8_t simSlot,
    const String &errorCode,
    const String &errorMsg,
    const String &providerMsgId
) {
    ApiResponse result = { false, "", "", 0 };

    JsonDocument doc;
    doc["attemptId"] = attemptId;
    doc["status"] = status;
    doc["hasCarrierDlr"] = hasCarrierDlr;

    if (simSlot >= 0) doc["simSlot"] = simSlot;
    if (!errorCode.isEmpty()) doc["errorCode"] = errorCode;
    if (!errorMsg.isEmpty()) doc["errorMessage"] = errorMsg;
    if (!providerMsgId.isEmpty()) doc["providerMsgId"] = providerMsgId;

    String body;
    serializeJson(doc, body);

    String response;
    result.httpCode = httpRequest("POST", API_RESULT, body, response);

    if (result.httpCode == 200) {
        JsonDocument resDoc;
        if (!deserializeJson(resDoc, response)) {
            result.success = resDoc["success"] | false;
            result.effectiveStatus = resDoc["effectiveStatus"] | "";
        }
    } else {
        result.error = "HTTP " + String(result.httpCode);
        LOG_E(TAG, "Report result failed for %s: %s", attemptId.c_str(), result.error.c_str());
    }

    return result;
}

// ─── Report Incoming SMS ────────────────────────────────────────────────────
ApiResponse ApiClient::reportIncoming(
    const String &from,
    const String &to,
    const String &message,
    const String &timestamp,
    int8_t simSlot
) {
    ApiResponse result = { false, "", "", 0 };

    JsonDocument doc;
    doc["from"] = from;
    doc["to"] = to;
    doc["message"] = message;
    doc["timestamp"] = timestamp.isEmpty() ? String("") : timestamp;
    if (simSlot >= 0) doc["simSlot"] = simSlot;

    String body;
    serializeJson(doc, body);

    String response;
    result.httpCode = httpRequest("POST", API_INCOMING, body, response);

    if (result.httpCode == 200) {
        result.success = true;
    } else {
        result.error = "HTTP " + String(result.httpCode);
    }

    return result;
}
