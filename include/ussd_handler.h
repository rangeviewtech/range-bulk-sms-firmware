// ============================================================================
// ussd_handler.h — USSD balance checking per SIM
// ============================================================================
#pragma once
#include <Arduino.h>
#include "sim_manager.h"

namespace UssdHandler {
    void begin(SimManager *simMgr);
    void update();                     // Call from main loop
    String getBalance(uint8_t slot);   // Get last known balance for a SIM
}
