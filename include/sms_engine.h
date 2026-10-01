// ============================================================================
// sms_engine.h — SMS send queue, routing, delivery tracking
// ============================================================================
#pragma once

#include <Arduino.h>
#include "storage.h"
#include "sim_manager.h"
#include "api_client.h"
#include <map>

// Tracks a sent SMS waiting for delivery report
struct PendingSend {
    String   attemptId;
    uint8_t  simSlot;
    uint8_t  messageRef;   // Modem reference number for DLR matching
    uint32_t sentAt;       // millis() when sent
    bool     reportedSubmitted; // Already reported SUBMITTED_TO_MODEM
};

class SmsEngine {
public:
    SmsEngine(SimManager &simMgr);

    // ─── Core Loop ──────────────────────────────────────────────────────
    // Process the send queue — called from main loop
    // Respects maxThroughput rate limiting
    void processSendQueue();

    // ─── Queue Management ───────────────────────────────────────────────
    // Add jobs fetched from backend
    void enqueueJobs(const std::vector<QueuedMessage> &jobs);

    // Get count of pending jobs
    size_t pendingCount() const { return _jobQueue.size(); }

    // ─── Delivery Report Processing ─────────────────────────────────────
    // Match a delivery report from modem to a pending send
    void processDeliveryReport(uint8_t simSlot, const DeliveryReport &report);

    // Check for and handle timed-out sends (no DLR after 5 minutes)
    void checkTimeouts();

    // ─── Rate Limiting ──────────────────────────────────────────────────
    void setMaxThroughput(int smsPerMinute) { _maxThroughput = smsPerMinute; }

    // ─── Persistence ────────────────────────────────────────────────────
    void saveState();
    void loadState();

    // ─── Stats ──────────────────────────────────────────────────────────
    uint32_t getSentCount() const { return _sentCount; }
    uint32_t getFailCount() const { return _failCount; }

private:
    SimManager &_simMgr;

    // Jobs waiting to be sent
    std::vector<SmsJob> _jobQueue;

    // Tracking sent messages awaiting DLR
    std::vector<PendingSend> _pendingSends;

    // Rate limiting
    int      _maxThroughput = 30;   // SMS per minute
    uint32_t _sendTimestamps[60];   // Circular buffer of send times
    uint8_t  _sendTimestampIdx = 0;

    // Stats for current session
    uint32_t _sentCount = 0;
    uint32_t _failCount = 0;

    // Check if we're within rate limit
    bool canSend() const;

    // Send a single SMS job
    bool sendJob(SmsJob &job);

    // Record a send timestamp for rate limiting
    void recordSendTime();

    // Check if a job has expired
    bool isExpired(const SmsJob &job) const;
};
