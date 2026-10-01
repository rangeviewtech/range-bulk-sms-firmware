// ============================================================================
// sim_manager.h — Multi-SIM orchestration, health monitoring, and blacklisting
// ============================================================================
#pragma once

#include <Arduino.h>
#include "config.h"
#include "modem_manager.h"
#include <vector>

// Health state of a single SIM slot
enum class SimHealth : uint8_t {
    UNKNOWN,       // Not yet probed
    HEALTHY,       // Working normally
    DEGRADED,      // Some failures but still usable
    BUSY,          // Currently sending an SMS
    DEAD,          // No network registration or SIM absent
    BLACKLISTED    // Too many failures, admin must unflag
};

// Per-SIM statistics and state
struct SimSlotState {
    uint8_t     slotIndex;
    SimHealth   health;
    ModemInfo   info;
    ModemManager *modem;       // Pointer to this slot's modem interface

    // Failure tracking for blacklisting
    uint32_t    failCount;
    uint32_t    failWindowStart; // millis() when first failure in window occurred
    uint32_t    totalSent;
    uint32_t    totalFailed;
    uint32_t    totalDelivered;
    uint32_t    lastSendTime;    // millis() of last send attempt
    uint32_t    consecutiveTimeouts; // AT command timeouts in a row
    uint32_t    lastRecoveryProbe;   // millis() of last blacklist recovery check

    // Admin unflag support
    bool        adminUnflagged;  // Set by backend config poll
};

class SimManager {
public:
    SimManager();
    ~SimManager();

    // ─── Initialization ─────────────────────────────────────────────────
    // Probe all possible UART ports and detect connected SIM modules
    uint8_t begin();

    // Get count of detected SIM modules
    uint8_t getSlotCount() const { return _activeSlots; }

    // Get state of a specific slot
    const SimSlotState* getSlot(uint8_t index) const;

    // ─── SIM Selection ──────────────────────────────────────────────────
    // Pick the best available SIM for sending (round-robin among healthy)
    // Returns -1 if no SIM is available
    int8_t selectBestSim();

    // Mark a SIM as busy (sending)
    void markBusy(uint8_t slot);

    // Mark a SIM as available again
    void markAvailable(uint8_t slot);

    // ─── Health Management ──────────────────────────────────────────────
    // Record a send success for a SIM
    void recordSuccess(uint8_t slot);

    // Record a send failure for a SIM
    void recordFailure(uint8_t slot);

    // Record an AT timeout for a SIM
    void recordTimeout(uint8_t slot);

    // Check if a SIM should be blacklisted based on recent failures
    void evaluateHealth(uint8_t slot);

    // Admin unflag: restore a blacklisted SIM
    void unflag(uint8_t slot);

    // ─── Periodic Tasks ─────────────────────────────────────────────────
    // Call periodically to:
    // - Refresh signal quality on all SIMs
    // - Probe blacklisted SIMs for recovery
    // - Process URCs from all modems
    void update();

    // ─── Status Reporting ───────────────────────────────────────────────
    // Get best signal strength across all healthy SIMs (for heartbeat)
    int16_t getBestSignalDbm() const;

    // Get network operator of best SIM
    String getBestOperator() const;

    // Build JSON status for persistence / admin display
    void toJson(JsonDocument &doc) const;

    // Restore state from persisted JSON
    void fromJson(const JsonDocument &doc);

    // ─── URC Processing ─────────────────────────────────────────────────
    // Process unsolicited result codes from all modems
    // Returns incoming SMS and delivery reports
    struct UrcEvents {
        std::vector<std::pair<uint8_t, DeliveryReport>> deliveryReports; // (slot, report)
        std::vector<std::pair<uint8_t, IncomingSms>>    incomingSms;     // (slot, sms)
    };
    UrcEvents processAllURCs();

private:
    SimSlotState _slots[MAX_SIM_SLOTS];
    ModemManager _modems[MAX_SIM_SLOTS]; // Actual modem instances
    uint8_t  _activeSlots = 0;
    uint8_t  _roundRobinIndex = 0;

    // Hardware serial ports (only 2 available beyond Serial0)
    HardwareSerial *_hwSerial1 = nullptr;
    HardwareSerial *_hwSerial2 = nullptr;

    // Probe a single UART port
    bool probeSlot(uint8_t slot, HardwareSerial *serial, int rxPin, int txPin, uint32_t baud);
};
