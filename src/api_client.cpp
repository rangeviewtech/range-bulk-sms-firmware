// ============================================================================
// api_client.cpp — Backend REST API client implementation
// ============================================================================

#include "api_client.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFi.h>

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

    int httpCode;
    if (method == "POST") {
        httpCode = http.POST(body);
    } else if (method == "GET") {
        httpCode = http.GET();
    } else {
        http.end();
        return -1;
    }

    if (httpCode > 0) {
        response = http.getString();
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
ApiResponse ApiClient::heartbeat(int16_t signalDbm, const String &networkOperator) {
    ApiResponse result = { false, "", "", 0 };

    JsonDocument doc;
    // ESP32 typically doesn't have a battery — send null
    doc["batteryLevel"] = (char*)nullptr;
    doc["isCharging"] = false;
    doc["signalStrength"] = signalDbm;
    doc["networkOperator"] = networkOperator;

    String body;
    serializeJson(doc, body);

    String response;
    result.httpCode = httpRequest("POST", API_HEARTBEAT, body, response);

    if (result.httpCode == 200) {
        JsonDocument resDoc;
        if (!deserializeJson(resDoc, response)) {
            result.success = resDoc["success"] | false;
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
