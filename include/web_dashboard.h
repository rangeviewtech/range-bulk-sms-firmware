// web_dashboard.h
#pragma once
#include <Arduino.h>
#include "sim_manager.h"
#include "storage.h"

namespace WebDashboard {
    void begin(SimManager *simMgr, DeviceConfig *cfg, DeviceStats *stats);
    void handle();  // Call from loop
    void stop();
    bool isRunning();
}
