# Range View Technologies — ESP32 SMS Gateway Firmware

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)](https://platformio.org/)
[![License](https://img.shields.io/badge/license-proprietary-blue)](LICENSE)

Production-grade ESP32 firmware that transforms an ESP32 microcontroller into a multi-SIM SMS gateway appliance for the **Range View Technologies Bulk SMS Platform**.

## 🏗 Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    ESP32 SMS Gateway                      │
│                                                          │
│  ┌─────────┐  ┌──────────┐  ┌────────────────────────┐ │
│  │ WiFi    │  │ SIM Mgr  │  │ SMS Engine             │ │
│  │ Manager │  │ (Multi)  │  │ Queue → Route → Send   │ │
│  └────┬────┘  └────┬─────┘  └────────────┬───────────┘ │
│       │            │                      │             │
│  ┌────┴────┐  ┌────┴─────┐  ┌────────────┴───────────┐ │
│  │ API     │  │ Modem    │  │ Delivery Tracker       │ │
│  │ Client  │  │ Manager  │  │ DLR Matching           │ │
│  └────┬────┘  │ (AT Cmd) │  └────────────────────────┘ │
│       │       └────┬─────┘                              │
│  ┌────┴────┐  ┌────┴─────┐  ┌────────────────────────┐ │
│  │ Storage │  │ HW UART  │  │ LED Status / OTA       │ │
│  │ LittleFS│  │ SIM800L/ │  │ Indicators / Updates   │ │
│  └─────────┘  │ SIM7600  │  └────────────────────────┘ │
│               └──────────┘                              │
└──────────────────────────────────────────────────────────┘
         │                     │
    WiFi (HTTP)          UART (AT Commands)
         │                     │
    ┌────┴────┐          ┌─────┴─────┐
    │ Backend │          │ GSM       │
    │ Server  │          │ Network   │
    └─────────┘          └───────────┘
```

## ✨ Features

### Multi-SIM Support (1–10 SIMs)
- Auto-detects connected SIM modules on boot
- 2 hardware UARTs (SIM800L/SIM7600) built-in
- Extensible to 10 SIMs via SC16IS752 I2C UART expanders
- Per-SIM health monitoring and statistics

### Smart SMS Routing
- Round-robin load distribution across healthy SIMs
- Automatic failover when a SIM is busy or dead
- Rate limiting respects backend `maxThroughput` setting
- Queue persistence survives power loss

### Line Health Management
- **Busy Detection**: Switches to next SIM if current is sending
- **Dead Line Detection**: Marks SIMs with no network registration
- **Auto-Blacklisting**: >5 failures in 10 minutes → blacklisted
- **Recovery Probing**: Checks blacklisted SIMs every 5 minutes
- **Admin Unflagging**: Backend can restore blacklisted SIMs

### Delivery Status Monitoring
- Carrier DLR via `+CDS` status reports
- Message reference matching for accurate tracking
- Reports SUBMITTED → SENT → DELIVERED/FAILED lifecycle
- 5-minute DLR timeout for unconfirmed sends

### Incoming SMS Forwarding
- Captures SMS via `+CMT`/`+CMTI` notifications
- Forwards to backend for webhook dispatch
- Auto-deletes from SIM after forwarding

### WiFi Provisioning
- First boot: AP mode with captive portal (`RangeGW-XXXX`)
- Enter WiFi credentials, API URL, and pairing code
- mDNS: Access at `rangegw-XXXX.local`
- Exponential backoff WiFi reconnection

### System Reliability
- Hardware watchdog timer (30s)
- Persistent config, queue, and SIM state (LittleFS)
- OTA firmware updates via ArduinoOTA
- Low-memory alerts and heap monitoring
- LED status indicators for visual feedback

## 🔧 Hardware Requirements

### Minimum
- ESP32 DevKitC (or equivalent)
- 1× SIM800L or SIM7600 GSM module
- SIM card with SMS capability
- 5V power supply (2A recommended for SIM800L)
- WiFi network

### Recommended Multi-SIM Setup
- ESP32-WROOM-32
- 2× SIM800L modules (one per hardware UART)
- Level shifter (SIM800L uses 2.8V logic)
- Adequate power supply (each SIM800L peaks at 2A during TX)

### Pin Connections

| Function | ESP32 Pin | Notes |
|----------|-----------|-------|
| SIM0 RX  | GPIO 16   | UART1 RX |
| SIM0 TX  | GPIO 17   | UART1 TX |
| SIM1 RX  | GPIO 26   | UART2 RX |
| SIM1 TX  | GPIO 27   | UART2 TX |
| Status LED | GPIO 2  | Built-in LED |
| Modem RST | GPIO 4   | Optional, active LOW |
| I2C SDA  | GPIO 21   | For UART expanders |
| I2C SCL  | GPIO 22   | For UART expanders |

## 🚀 Getting Started

### 1. Install PlatformIO

```bash
pip install platformio
# or install the VS Code PlatformIO extension
```

### 2. Build & Upload

```bash
# Build
pio run

# Upload firmware
pio run --target upload

# Upload LittleFS data (captive portal files)
pio run --target uploadfs

# Monitor serial output
pio device monitor
```

### 3. First-Time Setup

1. Power on the ESP32 — it will create a WiFi AP: `RangeGW-XXXX`
2. Connect to the AP with your phone/computer
3. The captive portal will open automatically
4. Enter your WiFi credentials
5. Enter the API server URL (default: `http://192.168.1.100:3000/api/v1`)
6. Enter the **pairing code** from your Range View dashboard
7. The ESP32 will connect to WiFi and register with the backend

### 4. Serial Registration (Alternative)

If the captive portal doesn't capture the pairing code, you can enter it via Serial Monitor:
1. Open Serial Monitor at 115200 baud
2. When prompted, type the pairing code and press Enter

## 📡 Backend API Integration

The firmware integrates with these backend endpoints:

| Endpoint | Method | Auth | Purpose |
|----------|--------|------|---------|
| `/device/gateways/register` | POST | None | Pair device |
| `/device/gateways/heartbeat` | POST | Bearer | Report status |
| `/device/gateways/queue` | GET | Bearer | Claim SMS jobs |
| `/device/gateways/messages/result` | POST | Bearer | Report delivery |
| `/device/gateways/messages/incoming` | POST | Bearer | Forward SMS |

## 📊 LED Status Indicators

| Pattern | Meaning |
|---------|---------|
| Fast blink (100ms) | WiFi connecting |
| Slow blink (1s) | WiFi connected, not paired |
| Solid ON | Online and ready |
| Double blink | Sending SMS |
| Very fast blink (50ms) | Error state |
| Triple blink | AP/Portal mode |
| Breathing | OTA update in progress |

## 🔄 OTA Updates

The firmware supports ArduinoOTA for wireless updates:

```bash
pio run --target upload --upload-port rangegw-XXXX.local
```

## 📁 Project Structure

```
range-bulk-sms-firmware/
├── platformio.ini          # Build configuration
├── README.md               # This file
├── include/
│   ├── config.h            # Pin definitions, constants, log macros
│   ├── storage.h           # LittleFS persistence interface
│   ├── led_status.h        # LED indicator patterns
│   ├── modem_manager.h     # Low-level AT command interface
│   ├── sim_manager.h       # Multi-SIM orchestration
│   ├── sms_engine.h        # SMS queue and routing engine
│   ├── api_client.h        # Backend REST API client
│   ├── wifi_manager.h      # WiFi + captive portal
│   └── ota_updater.h       # OTA firmware updates
├── src/
│   ├── main.cpp            # Entry point, boot sequence, sync loop
│   ├── storage.cpp         # LittleFS implementation
│   ├── led_status.cpp      # LED pattern implementation
│   ├── modem_manager.cpp   # GSM modem AT commands
│   ├── sim_manager.cpp     # Multi-SIM health & routing
│   ├── sms_engine.cpp      # Send queue & delivery tracking
│   ├── api_client.cpp      # HTTP API client
│   ├── wifi_manager.cpp    # WiFi management
│   └── ota_updater.cpp     # ArduinoOTA handler
├── data/
│   └── portal.html         # Captive portal UI
└── .gitignore
```

## 📜 License

Proprietary — Range View Technologies. All rights reserved.
