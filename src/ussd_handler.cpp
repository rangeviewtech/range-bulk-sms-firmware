// ============================================================================
// ussd_handler.cpp — USSD balance checking per SIM
// Periodically sends AT+CUSD commands to check operator balance
// ============================================================================

#include "ussd_handler.h"
#include "config.h"

static const char *TAG = "USSD";

namespace UssdHandler {
    static SimManager* _simMgr = nullptr;
    static unsigned long lastUpdate = 0;

    // Store balance per slot (up to MAX_SIM_SLOTS)
    static String _balances[MAX_SIM_SLOTS];

    void begin(SimManager *simMgr) {
        _simMgr = simMgr;
        // Don't check immediately at boot — wait for modem stabilization
        lastUpdate = millis();
        for (int i = 0; i < MAX_SIM_SLOTS; i++) _balances[i] = "";
    }

    void update() {
        if (!_simMgr) return;

        unsigned long now = millis();
        if (now - lastUpdate < USSD_POLL_INTERVAL_MS) return;
        lastUpdate = now;

        LOG_D(TAG, "Polling USSD balance on all SIMs...");

        for (uint8_t i = 0; i < _simMgr->getSlotCount(); i++) {
            const SimSlotState* slot = _simMgr->getSlot(i);
            if (!slot) continue;

            // Only query HEALTHY SIMs that aren't busy
            if (slot->health != SimHealth::HEALTHY) continue;
            if (!slot->modem || !slot->modem->isReady()) continue;

            LOG_D(TAG, "USSD query on SIM %d: %s", i, USSD_BALANCE_CODE);
            String balance = slot->modem->sendUSSD(USSD_BALANCE_CODE);

            if (balance.length() > 0) {
                _balances[i] = balance;
                // Update the modem's info struct directly via a mutable access
                // The SimSlotState.info is the source of truth for the heartbeat
                // We use const_cast here because the slot pointer is const but
                // we're the authorized USSD handler updating balance data
                SimSlotState* mutableSlot = const_cast<SimSlotState*>(slot);
                mutableSlot->info.ussdBalance = balance;
                LOG_I(TAG, "SIM %d balance: %s", i, balance.c_str());
            } else {
                LOG_D(TAG, "SIM %d USSD returned empty", i);
            }
        }
    }

    String getBalance(uint8_t slot) {
        if (slot >= MAX_SIM_SLOTS) return "";
        return _balances[slot];
    }
}
