// ============================================================================
// api_client.h — Backend REST API client
// ============================================================================
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "storage.h"
#include <vector>

// Response from queue endpoint
struct QueuedMessage {
    String attemptId;
    String messageId;
    String recipientId;
    String phone;
    String message;
    String expiresAt;
    String encoding;
};

struct QueueResponse {
    bool success;
    std::vector<QueuedMessage> messages;
    int  maxThroughput;
    String error;
};

// Response from register endpoint
struct RegisterResponse {
    bool   success;
    String gatewayId;
    String token;
    String error;
};

// Response from result/heartbeat endpoints
struct ApiResponse {
    bool   success;
    String effectiveStatus;
    String error;
    int    httpCode;
};

namespace ApiClient {
    // Configure the client with base URL and auth token
    void configure(const String &baseUrl, const String &token, const String &rootCa);

    // ─── Endpoints ──────────────────────────────────────────────────────

    // Register device with pairing code (no auth token needed yet)
    RegisterResponse registerDevice(const String &baseUrl, const String &pairingCode, const String &rootCa);

    // Send heartbeat with device status
    ApiResponse heartbeat(class SimManager* simMgr, struct DeviceStats* stats);

    // Fetch queued messages
    QueueResponse fetchQueue(int limit = 10);

    // Report message delivery result
    ApiResponse reportResult(
        const String &attemptId,
        const String &status,        // SUBMITTED_TO_MODEM, SENT, DELIVERED, FAILED
        bool hasCarrierDlr = false,
        int8_t simSlot = -1,
        const String &errorCode = "",
        const String &errorMsg = "",
        const String &providerMsgId = ""
    );

    // Report incoming SMS
    ApiResponse reportIncoming(
        const String &from,
        const String &to,
        const String &message,
        const String &timestamp,
        int8_t simSlot = -1
    );

    // ─── Status ─────────────────────────────────────────────────────────
    bool isConfigured();
}
