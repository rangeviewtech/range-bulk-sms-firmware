// ============================================================================
// sim_manager.cpp — Multi-SIM orchestration implementation
// ============================================================================

#include "sim_manager.h"
#include "config.h"

static const char *TAG = "SIM_MGR";

SimManager::SimManager() {
    for (int i = 0; i < MAX_SIM_SLOTS; i++) {
        _slots[i].slotIndex = i;
        _slots[i].health = SimHealth::UNKNOWN;
        _slots[i].modem = &_modems[i];
        _slots[i].failCount = 0;
        _slots[i].failWindowStart = 0;
        _slots[i].totalSent = 0;
        _slots[i].totalFailed = 0;
        _slots[i].totalDelivered = 0;
        _slots[i].lastSendTime = 0;
        _slots[i].consecutiveTimeouts = 0;
        _slots[i].lastRecoveryProbe = 0;
        _slots[i].adminUnflagged = false;
    }
}

SimManager::~SimManager() {
    // Nothing to clean — modems are stack-allocated
}

// ─── Initialization ─────────────────────────────────────────────────────────

uint8_t SimManager::begin() {
    LOG_I(TAG, "Scanning for SIM modules...");
    _activeSlots = 0;

    // Probe SIM Slot 0: Hardware UART1
    _hwSerial1 = &Serial1;
    if (probeSlot(0, _hwSerial1, SIM0_RX_PIN, SIM0_TX_PIN, SIM0_BAUD)) {
        _activeSlots++;
        LOG_I(TAG, "SIM Slot 0: DETECTED on UART1");
    } else {
        LOG_D(TAG, "SIM Slot 0: not found");
    }

    // Probe SIM Slot 1: Hardware UART2
    _hwSerial2 = &Serial2;
    if (probeSlot(1, _hwSerial2, SIM1_RX_PIN, SIM1_TX_PIN, SIM1_BAUD)) {
        _activeSlots++;
        LOG_I(TAG, "SIM Slot 1: DETECTED on UART2");
    } else {
        LOG_D(TAG, "SIM Slot 1: not found");
    }

    // NOTE: Slots 2-9 would use SC16IS752 I2C UART expanders.
    // This requires the SC16IS752 library and I2C scanning.
    // For the initial firmware version, we support 2 hardware UART slots.
    // Additional slots can be added by implementing I2C UART bridging.

    LOG_I(TAG, "Detected %d SIM module(s)", _activeSlots);
    return _activeSlots;
}

bool SimManager::probeSlot(uint8_t slot, HardwareSerial *serial, int rxPin, int txPin, uint32_t baud) {
    _modems[slot].begin(serial, rxPin, txPin, baud);
    delay(MODEM_INIT_DELAY_MS);

    if (!_modems[slot].probe()) {
        _slots[slot].health = SimHealth::UNKNOWN;
        return false;
    }

    if (_modems[slot].initialize()) {
        _slots[slot].health = SimHealth::HEALTHY;
        _slots[slot].info = _modems[slot].getInfo();
        LOG_I(TAG, "  Slot %d: operator=%s, signal=%d dBm, IMEI=%s",
              slot, _slots[slot].info.operatorName.c_str(),
              _slots[slot].info.signalDbm,
              _slots[slot].info.imei.c_str());
    } else {
        _slots[slot].health = SimHealth::DEAD;
        LOG_W(TAG, "  Slot %d: init failed (DEAD)", slot);
    }

    return true;
}

// ─── Accessors ──────────────────────────────────────────────────────────────

const SimSlotState* SimManager::getSlot(uint8_t index) const {
    if (index >= MAX_SIM_SLOTS) return nullptr;
    return &_slots[index];
}

// ─── SIM Selection ──────────────────────────────────────────────────────────

int8_t SimManager::selectBestSim() {
    // Round-robin among HEALTHY SIMs
    uint8_t startIdx = _roundRobinIndex;
    for (uint8_t i = 0; i < _activeSlots; i++) {
        uint8_t idx = (startIdx + i) % _activeSlots;
        if (_slots[idx].health == SimHealth::HEALTHY) {
            _roundRobinIndex = (idx + 1) % _activeSlots;
            return (int8_t)idx;
        }
    }

    // Fallback: try DEGRADED SIMs
    for (uint8_t i = 0; i < _activeSlots; i++) {
        if (_slots[i].health == SimHealth::DEGRADED) {
            return (int8_t)i;
        }
    }

    LOG_W(TAG, "No available SIM slot!");
    return -1;
}

void SimManager::markBusy(uint8_t slot) {
    if (slot < _activeSlots) {
        _slots[slot].health = SimHealth::BUSY;
        _slots[slot].lastSendTime = millis();
    }
}

void SimManager::markAvailable(uint8_t slot) {
    if (slot < _activeSlots && _slots[slot].health == SimHealth::BUSY) {
        _slots[slot].health = SimHealth::HEALTHY;
    }
}

// ─── Health Management ──────────────────────────────────────────────────────

void SimManager::recordSuccess(uint8_t slot) {
    if (slot >= _activeSlots) return;

    _slots[slot].totalSent++;
    _slots[slot].consecutiveTimeouts = 0;

    // Reset failure window on success
    _slots[slot].failCount = 0;
    _slots[slot].failWindowStart = 0;

    if (_slots[slot].health == SimHealth::DEGRADED) {
        _slots[slot].health = SimHealth::HEALTHY;
        LOG_I(TAG, "Slot %d recovered to HEALTHY after success", slot);
    }
}

void SimManager::recordFailure(uint8_t slot) {
    if (slot >= _activeSlots) return;

    _slots[slot].totalFailed++;
    uint32_t now = millis();

    // Start or continue failure window
    if (_slots[slot].failCount == 0 || (now - _slots[slot].failWindowStart) > BLACKLIST_WINDOW_MS) {
        // Reset window
        _slots[slot].failWindowStart = now;
        _slots[slot].failCount = 1;
    } else {
        _slots[slot].failCount++;
    }

    LOG_W(TAG, "Slot %d failure #%d in window", slot, _slots[slot].failCount);

    // Mark as degraded before blacklist threshold
    if (_slots[slot].health == SimHealth::HEALTHY && _slots[slot].failCount >= 2) {
        _slots[slot].health = SimHealth::DEGRADED;
        LOG_W(TAG, "Slot %d marked DEGRADED", slot);
    }

    evaluateHealth(slot);
}

void SimManager::recordTimeout(uint8_t slot) {
    if (slot >= _activeSlots) return;

    _slots[slot].consecutiveTimeouts++;
    LOG_W(TAG, "Slot %d AT timeout #%d", slot, _slots[slot].consecutiveTimeouts);

    if (_slots[slot].consecutiveTimeouts >= 3) {
        _slots[slot].health = SimHealth::DEAD;
        LOG_E(TAG, "Slot %d marked DEAD after %d consecutive timeouts",
              slot, _slots[slot].consecutiveTimeouts);
    }
}

void SimManager::evaluateHealth(uint8_t slot) {
    if (slot >= _activeSlots) return;

    // Check blacklist threshold
    if (_slots[slot].failCount >= BLACKLIST_FAIL_THRESHOLD &&
        _slots[slot].health != SimHealth::BLACKLISTED) {

        _slots[slot].health = SimHealth::BLACKLISTED;
        _slots[slot].lastRecoveryProbe = millis();
        LOG_E(TAG, "Slot %d BLACKLISTED: %d failures in %d ms window",
              slot, _slots[slot].failCount, BLACKLIST_WINDOW_MS);
    }

    // Check network registration
    if (_slots[slot].health != SimHealth::BLACKLISTED) {
        NetRegStatus netStat = _modems[slot].getNetworkStatus();
        if (netStat == NetRegStatus::NOT_REGISTERED ||
            netStat == NetRegStatus::DENIED) {
            if (_slots[slot].health != SimHealth::DEAD) {
                _slots[slot].health = SimHealth::DEAD;
                LOG_E(TAG, "Slot %d DEAD: network status=%d", slot, (int)netStat);
            }
        }
    }
}

void SimManager::unflag(uint8_t slot) {
    if (slot >= _activeSlots) return;

    if (_slots[slot].health == SimHealth::BLACKLISTED ||
        _slots[slot].health == SimHealth::DEAD) {
        _slots[slot].failCount = 0;
        _slots[slot].failWindowStart = 0;
        _slots[slot].consecutiveTimeouts = 0;
        _slots[slot].adminUnflagged = true;

        // Re-probe the modem
        if (_modems[slot].probe() && _modems[slot].initialize()) {
            _slots[slot].health = SimHealth::HEALTHY;
            _slots[slot].info = _modems[slot].getInfo();
            LOG_I(TAG, "Slot %d UNFLAGGED and recovered by admin", slot);
        } else {
            _slots[slot].health = SimHealth::DEAD;
            LOG_W(TAG, "Slot %d unflagged but modem still not responding", slot);
        }
    }
}

// ─── Periodic Tasks ─────────────────────────────────────────────────────────

void SimManager::update() {
    uint32_t now = millis();

    for (uint8_t i = 0; i < _activeSlots; i++) {
        // Process URCs from all active modems
        if (_slots[i].health != SimHealth::UNKNOWN) {
            _modems[i].processURC();
        }

        // Recovery probe for blacklisted SIMs
        if (_slots[i].health == SimHealth::BLACKLISTED &&
            (now - _slots[i].lastRecoveryProbe) >= BLACKLIST_RECOVERY_MS) {

            _slots[i].lastRecoveryProbe = now;
            LOG_I(TAG, "Recovery probe for blacklisted slot %d...", i);

            // Check if network is available now
            NetRegStatus netStat = _modems[i].getNetworkStatus();
            if (netStat == NetRegStatus::REGISTERED_HOME ||
                netStat == NetRegStatus::REGISTERED_ROAMING) {

                int8_t csq = _modems[i].getSignalQuality();
                if (csq > 0 && csq != 99) {
                    // Looks alive — restore to DEGRADED (not HEALTHY yet)
                    _slots[i].health = SimHealth::DEGRADED;
                    _slots[i].failCount = 0;
                    _slots[i].failWindowStart = 0;
                    LOG_I(TAG, "Slot %d auto-recovered from blacklist to DEGRADED", i);
                }
            }
        }

        // Recovery probe for dead SIMs (less frequently)
        if (_slots[i].health == SimHealth::DEAD &&
            (now - _slots[i].lastRecoveryProbe) >= (BLACKLIST_RECOVERY_MS * 2)) {

            _slots[i].lastRecoveryProbe = now;
            LOG_D(TAG, "Probing dead slot %d...", i);

            if (_modems[i].probe()) {
                _modems[i].initialize();
                NetRegStatus netStat = _modems[i].getNetworkStatus();
                if (netStat == NetRegStatus::REGISTERED_HOME ||
                    netStat == NetRegStatus::REGISTERED_ROAMING) {
                    _slots[i].health = SimHealth::DEGRADED;
                    _slots[i].consecutiveTimeouts = 0;
                    _slots[i].info = _modems[i].getInfo();
                    LOG_I(TAG, "Dead slot %d resurrected to DEGRADED", i);
                }
            }
        }

        // Refresh signal info periodically for healthy/degraded SIMs
        if ((_slots[i].health == SimHealth::HEALTHY ||
             _slots[i].health == SimHealth::DEGRADED) &&
            (now - _slots[i].lastSendTime) > 30000) { // Every 30s if idle
            _slots[i].info.signalDbm = _modems[i].getSignalDbm();
            _slots[i].info.signalRSSI = _modems[i].getSignalQuality();
            _slots[i].info.signalBars = csqToBars(_slots[i].info.signalRSSI);
            _slots[i].info.networkType = getNetworkType(i);
            
            // Only fetch phone number if empty to save time
            if (_slots[i].info.phoneNumber.isEmpty()) {
                _slots[i].info.phoneNumber = getPhoneNumber(i);
            }
        }
    }
}

uint8_t SimManager::csqToBars(int8_t csq) {
    if (csq == 99 || csq < 0) return 0;
    if (csq >= 20) return 5;
    if (csq >= 15) return 4;
    if (csq >= 10) return 3;
    if (csq >= 5)  return 2;
    return 1;
}

String SimManager::getNetworkType(uint8_t slot) {
    if (slot < _activeSlots && _slots[slot].modem) {
        return _slots[slot].modem->getNetworkType();
    }
    return "UNKNOWN";
}

String SimManager::getPhoneNumber(uint8_t slot) {
    if (slot < _activeSlots && _slots[slot].modem) {
        return _slots[slot].modem->getPhoneNumber();
    }
    return "";
}

// ─── Status Reporting ───────────────────────────────────────────────────────

int16_t SimManager::getBestSignalDbm() const {
    int16_t best = -999;
    for (uint8_t i = 0; i < _activeSlots; i++) {
        if (_slots[i].health == SimHealth::HEALTHY ||
            _slots[i].health == SimHealth::DEGRADED ||
            _slots[i].health == SimHealth::BUSY) {
            if (_slots[i].info.signalDbm > best) {
                best = _slots[i].info.signalDbm;
            }
        }
    }
    return best;
}

String SimManager::getBestOperator() const {
    int16_t bestSignal = -999;
    String bestOp = "Unknown";
    for (uint8_t i = 0; i < _activeSlots; i++) {
        if ((_slots[i].health == SimHealth::HEALTHY ||
             _slots[i].health == SimHealth::DEGRADED) &&
            _slots[i].info.signalDbm > bestSignal) {
            bestSignal = _slots[i].info.signalDbm;
            bestOp = _slots[i].info.operatorName;
        }
    }
    return bestOp;
}

SimManager::UrcEvents SimManager::processAllURCs() {
    UrcEvents events;

    for (uint8_t i = 0; i < _activeSlots; i++) {
        if (_slots[i].health == SimHealth::UNKNOWN) continue;

        _modems[i].processURC();

        // Collect delivery reports
        while (_modems[i].hasDeliveryReport()) {
            DeliveryReport dlr = _modems[i].getDeliveryReport();
            events.deliveryReports.push_back({ i, dlr });
        }

        // Collect incoming SMS
        while (_modems[i].hasIncomingSms()) {
            IncomingSms sms = _modems[i].getIncomingSms();
            events.incomingSms.push_back({ i, sms });
        }
    }

    return events;
}

// ─── JSON Serialization ─────────────────────────────────────────────────────

void SimManager::toJson(JsonDocument &doc) const {
    JsonArray slots = doc["slots"].to<JsonArray>();
    for (uint8_t i = 0; i < _activeSlots; i++) {
        JsonObject obj = slots.add<JsonObject>();
        obj["slot"] = i;
        obj["health"] = (uint8_t)_slots[i].health;
        obj["operator"] = _slots[i].info.operatorName;
        obj["networkType"] = _slots[i].info.networkType;
        obj["phoneNumber"] = _slots[i].info.phoneNumber;
        obj["registrationStatus"] = _slots[i].info.registrationStatus;
        obj["ussdBalance"] = _slots[i].info.ussdBalance;
        obj["signalDbm"] = _slots[i].info.signalDbm;
        obj["signalBars"] = _slots[i].info.signalBars;
        obj["imei"] = _slots[i].info.imei;
        obj["iccid"] = _slots[i].info.iccid;
        obj["totalSent"] = _slots[i].totalSent;
        obj["totalFailed"] = _slots[i].totalFailed;
        obj["totalDelivered"] = _slots[i].totalDelivered;
        obj["failCount"] = _slots[i].failCount;
        obj["consecutiveTimeouts"] = _slots[i].consecutiveTimeouts;
    }
}

void SimManager::fromJson(const JsonDocument &doc) {
    JsonArrayConst slots = doc["slots"];
    if (slots.isNull()) return;

    for (JsonObjectConst obj : slots) {
        uint8_t slot = obj["slot"] | 255;
        if (slot >= _activeSlots) continue;

        _slots[slot].health = (SimHealth)(obj["health"] | 0);
        _slots[slot].totalSent = obj["totalSent"] | 0;
        _slots[slot].totalFailed = obj["totalFailed"] | 0;
        _slots[slot].totalDelivered = obj["totalDelivered"] | 0;
        _slots[slot].failCount = obj["failCount"] | 0;
        _slots[slot].consecutiveTimeouts = obj["consecutiveTimeouts"] | 0;
    }
    LOG_I(TAG, "Restored SIM state from flash");
}
