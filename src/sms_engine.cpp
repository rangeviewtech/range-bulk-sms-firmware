// ============================================================================
// sms_engine.cpp — SMS send queue, smart routing, and delivery tracking
// ============================================================================

#include "sms_engine.h"
#include "config.h"
#include <time.h>

static const char *TAG = "SMS_ENG";

SmsEngine::SmsEngine(SimManager &simMgr) : _simMgr(simMgr) {
    memset(_sendTimestamps, 0, sizeof(_sendTimestamps));
}

// ─── Core Send Loop ─────────────────────────────────────────────────────────

void SmsEngine::processSendQueue() {
    if (_jobQueue.empty()) return;

    // Remove expired jobs first
    for (auto it = _jobQueue.begin(); it != _jobQueue.end(); ) {
        if (isExpired(*it)) {
            LOG_W(TAG, "Job %s expired, removing", it->attemptId.c_str());
            // Report as FAILED due to expiry
            ApiClient::reportResult(it->attemptId, "FAILED", false, -1,
                                    "EXPIRED", "SMS expired before sending");
            _failCount++;
            it = _jobQueue.erase(it);
        } else {
            ++it;
        }
    }

    // Process jobs respecting rate limit
    while (!_jobQueue.empty() && canSend()) {
        SmsJob &job = _jobQueue.front();

        // Select best SIM
        int8_t simSlot = job.assignedSim;
        if (simSlot < 0) {
            simSlot = _simMgr.selectBestSim();
        }

        if (simSlot < 0) {
            LOG_W(TAG, "No available SIM for sending, waiting...");
            break;
        }

        // Attempt to send
        if (sendJob(job)) {
            _jobQueue.erase(_jobQueue.begin());
        } else {
            // Move to back of queue for retry
            job.retryCount++;
            job.assignedSim = -1; // Try different SIM next time
            if (job.retryCount >= MAX_AT_RETRIES) {
                LOG_E(TAG, "Job %s exceeded max retries, failing", job.attemptId.c_str());
                ApiClient::reportResult(job.attemptId, "FAILED", false, simSlot,
                                        "MAX_RETRIES", "Exceeded maximum send attempts");
                _failCount++;
                _jobQueue.erase(_jobQueue.begin());
            } else {
                SmsJob retryJob = job;
                _jobQueue.erase(_jobQueue.begin());
                _jobQueue.push_back(retryJob);
            }
            break; // Back off after failure
        }
    }
}

bool SmsEngine::sendJob(SmsJob &job) {
    int8_t simSlot = _simMgr.selectBestSim();
    if (simSlot < 0) return false;

    const SimSlotState *slot = _simMgr.getSlot(simSlot);
    if (!slot || !slot->modem) return false;

    LOG_I(TAG, "Sending SMS to %s via SIM %d (attempt #%d)",
          job.phone.c_str(), simSlot, job.retryCount + 1);

    LedStatus::setPattern(LedPattern::SENDING_SMS);
    _simMgr.markBusy(simSlot);

    // Report SUBMITTED_TO_MODEM
    ApiClient::reportResult(job.attemptId, "SUBMITTED_TO_MODEM", false, simSlot);

    // Send via modem
    SmsSendResult result = slot->modem->sendSMS(job.phone, job.message);

    _simMgr.markAvailable(simSlot);
    LedStatus::setPattern(LedPattern::ONLINE_READY);

    if (result.success) {
        _sentCount++;
        _simMgr.recordSuccess(simSlot);
        recordSendTime();

        // Track for DLR matching
        PendingSend pending;
        pending.attemptId = job.attemptId;
        pending.simSlot = simSlot;
        pending.messageRef = result.messageRef;
        pending.sentAt = millis();
        pending.reportedSubmitted = true;
        _pendingSends.push_back(pending);

        // Report SENT (conservative — await DLR for DELIVERED)
        ApiClient::reportResult(job.attemptId, "SENT", false, simSlot);

        LOG_I(TAG, "SMS sent successfully (ref=%d)", result.messageRef);
        return true;
    } else {
        _failCount++;
        _simMgr.recordFailure(simSlot);

        ApiClient::reportResult(job.attemptId,
                                result.errorCode == "SEND_UNCERTAIN" ? "SEND_UNCERTAIN" : "FAILED",
                                false, simSlot,
                                result.errorCode, result.errorMsg);

        LOG_E(TAG, "SMS send failed: %s - %s", result.errorCode.c_str(), result.errorMsg.c_str());
        return false;
    }
}

// ─── Queue Management ───────────────────────────────────────────────────────

void SmsEngine::enqueueJobs(const std::vector<QueuedMessage> &jobs) {
    for (const auto &qm : jobs) {
        SmsJob job;
        job.attemptId = qm.attemptId;
        job.messageId = qm.messageId;
        job.recipientId = qm.recipientId;
        job.phone = qm.phone;
        job.message = qm.message;
        job.expiresAt = qm.expiresAt;
        job.encoding = qm.encoding;
        job.assignedSim = -1;
        job.retryCount = 0;
        _jobQueue.push_back(job);
    }
    LOG_I(TAG, "Enqueued %d new jobs (total: %d)", jobs.size(), _jobQueue.size());
}

// ─── Delivery Report Processing ─────────────────────────────────────────────

void SmsEngine::processDeliveryReport(uint8_t simSlot, const DeliveryReport &report) {
    // Match by message reference number and SIM slot
    for (auto it = _pendingSends.begin(); it != _pendingSends.end(); ++it) {
        if (it->messageRef == report.messageRef && it->simSlot == simSlot) {
            if (report.delivered) {
                LOG_I(TAG, "DLR: %s DELIVERED (carrier confirmed)", it->attemptId.c_str());
                ApiClient::reportResult(it->attemptId, "DELIVERED", true, simSlot);
                _simMgr.recordSuccess(simSlot);
                const SimSlotState *slot = _simMgr.getSlot(simSlot);
                // We can't modify through a const pointer, but this is tracked in SimManager
            } else {
                LOG_W(TAG, "DLR: %s delivery FAILED (status=%d)", it->attemptId.c_str(), report.status);
                ApiClient::reportResult(it->attemptId, "FAILED", true, simSlot,
                                        "DLR_FAILED", "Carrier reported delivery failure");
            }
            _pendingSends.erase(it);
            return;
        }
    }
    LOG_D(TAG, "DLR for unknown ref=%d on SIM %d (may be old)", report.messageRef, simSlot);
}

void SmsEngine::checkTimeouts() {
    uint32_t now = millis();
    for (auto it = _pendingSends.begin(); it != _pendingSends.end(); ) {
        // If no DLR after 5 minutes, consider it SENT (not DELIVERED)
        if (now - it->sentAt > 300000) {
            LOG_D(TAG, "DLR timeout for %s (5 min), remaining as SENT", it->attemptId.c_str());
            it = _pendingSends.erase(it);
        } else {
            ++it;
        }
    }
}

// ─── Rate Limiting ──────────────────────────────────────────────────────────

bool SmsEngine::canSend() const {
    if (_maxThroughput <= 0) return true;

    uint32_t now = millis();
    uint32_t oneMinuteAgo = now - 60000;
    int recentSends = 0;

    for (int i = 0; i < 60; i++) {
        if (_sendTimestamps[i] > oneMinuteAgo) {
            recentSends++;
        }
    }

    return recentSends < _maxThroughput;
}

void SmsEngine::recordSendTime() {
    _sendTimestamps[_sendTimestampIdx] = millis();
    _sendTimestampIdx = (_sendTimestampIdx + 1) % 60;
}

bool SmsEngine::isExpired(const SmsJob &job) const {
    if (job.expiresAt.isEmpty()) return false;

    // Parse ISO8601 timestamp
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    // Format: "2024-01-15T10:30:00.000Z"
    if (sscanf(job.expiresAt.c_str(), "%d-%d-%dT%d:%d:%d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        time_t expiresEpoch = mktime(&tm);
        time_t now;
        time(&now);
        // Expire if within buffer
        return difftime(expiresEpoch, now) < SMS_EXPIRE_BUFFER_S;
    }

    return false; // Can't parse — don't expire
}

// ─── Persistence ────────────────────────────────────────────────────────────

void SmsEngine::saveState() {
    Storage::saveQueue(_jobQueue);
}

void SmsEngine::loadState() {
    Storage::loadQueue(_jobQueue);
    LOG_I(TAG, "Restored %d jobs from flash", _jobQueue.size());
}
