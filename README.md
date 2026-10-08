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
│  │ Storage │  │ HW UART  │  │ Web Dashboard / OTA    │ │
│  │ LittleFS│  │ SIM800L/ │  │ V380-style Config UI   │ │
│  └─────────┘  │ SIM7600  │  └────────────────────────┘ │
│               └──────────┘                              │
│  ┌─────────┐  ┌──────────┐  ┌────────────────────────┐ │
│  │ Power   │  │ USSD     │  │ LED Status             │ │
│  │ Monitor │  │ Handler  │  │ Indicators             │ │
│  └─────────┘  └──────────┘  └────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                     │
    WiFi / Ethernet       UART (AT Commands)
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
- Per-SIM health monitoring, operator name, signal bars, and statistics
- IMEI, ICCID, phone number readout per SIM

### Smart SMS Routing
- Round-robin load distribution across healthy SIMs
- Automatic failover when a SIM is busy or dead
- Rate limiting respects backend `maxThroughput` setting
- Multi-part SMS support for messages > 160 characters
- Queue persistence survives power loss

### Line Health Management
- **Busy Detection**: Switches to next SIM if current is sending
- **Dead Line Detection**: Marks SIMs with no network registration
- **Auto-Blacklisting**: >5 failures in 10 minutes → blacklisted
- **Recovery Probing**: Checks blacklisted SIMs every 5 minutes
- **Admin Unflagging**: Backend or web dashboard can restore blacklisted SIMs

### Delivery Status Monitoring
- Carrier DLR via `+CDS` status reports
- Message reference matching for accurate tracking
- Reports SUBMITTED → SENT → DELIVERED/FAILED lifecycle
- 5-minute DLR timeout for unconfirmed sends

### Incoming SMS Forwarding
- Captures SMS via `+CMT`/`+CMTI` notifications
- Forwards to backend for webhook dispatch
- Auto-deletes from SIM after forwarding

### Power & Battery Monitoring (NEW)
- Battery voltage monitoring via ADC with voltage divider
- USB power detection pin
- Li-ion percentage mapping (3.0V=0% → 4.2V=100%)
- ESP32 internal CPU temperature sensor
- Power source detection: Battery / USB / DC / PoE
- Reset reason tracking: POWERON, BROWNOUT, WDT, PANIC, etc.
- Low battery alerts (< 20%)

### Web Dashboard — V380-Style (NEW)
- Full HTML dashboard at `http://device-ip/`
- **System card**: Firmware, uptime, heap, CPU temp, reset reason
- **Power card**: Source, voltage, battery %, charging status
- **Connectivity card**: WiFi SSID, signal, IP, MAC, API server
- **Statistics card**: Sent, Delivered, Failed, Incoming counts
- **SIM card table**: Operator, Network Type (GSM/3G/LTE), Signal Bars (▊▊▊▊▊), Health badge, IMEI, phone, balance, per-SIM send/fail/deliver counts
- **Admin actions**: Restart, Factory Reset, Unflag SIM
- JSON API at `/api/status`
- Auto-refresh every 30 seconds
- Range View brand colors: Navy #07163D, Yellow #FBCA07, Blue #04648C

### USSD Balance Checking (NEW)
- Periodic USSD balance query per SIM (`*123#` configurable)
- Hourly polling interval
- Balance displayed in dashboard and heartbeat telemetry

### Network Type Detection (NEW)
- Reads AT+COPS? access technology field
- Displays GSM / EDGE / 3G / LTE per SIM
- Included in heartbeat and web dashboard

### Enhanced Heartbeat Telemetry (NEW)
Rich JSON payload sent to backend every 60 seconds:
- Power source, battery voltage, charging state
- WiFi SSID, RSSI, MAC, local IP
- Firmware version, free heap, CPU temperature
- Reset reason, uptime
- Per-SIM detailed array (operator, signal, bars, network type, health, IMEI, ICCID, phone, sent/failed/delivered, USSD balance)
- Backend returns admin commands (unflag SIMs remotely)

### Factory Reset (NEW)
- Hold BOOT button for 5 seconds on startup → factory reset
- Also available from web dashboard and backend
- Deletes all config, queue, SIM state, and stats

### WiFi Provisioning
- First boot: AP mode with captive portal (`RangeGW-XXXX`)
- Enter WiFi credentials, API URL, and pairing code
- mDNS: Access at `rangegw-XXXX.local`
- Exponential backoff WiFi reconnection

### Ethernet Support (Optional)
- W5500 SPI Ethernet module support (configurable pins)
- Toggle via `ETH_ENABLED` in config.h

### System Reliability
- Hardware watchdog timer (30s)
- Persistent config, queue, and SIM state (LittleFS)
- OTA firmware updates via ArduinoOTA
- Low-memory alerts and heap monitoring
- LED status indicators for visual feedback
- Incoming call rejection (`ATH`)

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

### Optional Add-ons
- Battery + voltage divider (100kΩ + 100kΩ) → GPIO 34
- USB detect wire → GPIO 35
- W5500 Ethernet module (SPI: GPIO 18/19/23/5)
- SC16IS752 I2C UART expanders for 3+ SIM slots

### Pin Connections

| Function | ESP32 Pin | Notes |
|----------|-----------|-------|
| SIM0 RX  | GPIO 16   | UART1 RX |
| SIM0 TX  | GPIO 17   | UART1 TX |
| SIM1 RX  | GPIO 26   | UART2 RX |
| SIM1 TX  | GPIO 27   | UART2 TX |
| Status LED | GPIO 2  | Built-in LED |
| Modem RST | GPIO 4   | Optional, active LOW |
| Battery ADC | GPIO 34 | ADC1, via voltage divider |
| USB Detect | GPIO 35  | HIGH when USB 5V present |
| Factory Reset | GPIO 0 | BOOT button, hold 5s |
| I2C SDA  | GPIO 21   | For UART expanders |
| I2C SCL  | GPIO 22   | For UART expanders |
| ETH CS   | GPIO 5    | W5500 chip select |
| ETH MOSI | GPIO 23   | W5500 SPI |
| ETH MISO | GPIO 19   | W5500 SPI |
| ETH SCLK | GPIO 18   | W5500 SPI |

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
5. Enter the web app API URL. Use the deployed web app origin in production
   (HTTPS required); local development must use a host address reachable by
   the ESP32. `/api/v1` is added automatically if omitted.
6. Enter the **pairing code** from your Range View dashboard
7. The ESP32 will connect to WiFi and register with the backend

### 4. Access the Dashboard

After setup, navigate to `http://<device-ip>/` to see:
- System status, power, connectivity
- SIM card details with operator, signal bars, network type
- SMS statistics
- Admin controls (restart, factory reset, unflag SIM)

## 📡 Backend API Integration

The firmware connects directly to the same Next.js web backend used by the
mobile and Android gateway apps. It uses the `API Server URL` entered in the
provisioning portal; no Redis credentials belong on the device. Production
connections must use HTTPS.
The provisioning portal also requires the trusted root CA certificate for the
backend's TLS certificate. Paste the PEM certificate as one line, replacing each
line break with the two characters `\n`; the device restores the line breaks
before saving. Pairing and all device traffic fail closed if the CA is missing.
Do not put the backend token or Redis credentials in the portal's API URL or CA
field. The firmware update checker stays disabled until the web backend has
`ESP32_FIRMWARE_VERSION` and `ESP32_FIRMWARE_DOWNLOAD_URL` configured to a real
HTTPS artifact.

The firmware integrates with these backend endpoints:

| Endpoint | Method | Auth | Purpose |
|----------|--------|------|---------|
| `/device/gateways/register` | POST | None | Pair device |
| `/device/gateways/heartbeat` | POST | Bearer | Report rich telemetry |
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

## 📁 Project Structure

```
range-bulk-sms-firmware/
├── platformio.ini          # Build configuration
├── README.md               # This file
├── include/
│   ├── config.h            # Pin defs, constants, thresholds
│   ├── storage.h           # LittleFS persistence
│   ├── led_status.h        # LED indicator patterns
│   ├── modem_manager.h     # AT command interface
│   ├── sim_manager.h       # Multi-SIM orchestration
│   ├── sms_engine.h        # SMS queue and routing
│   ├── api_client.h        # Backend REST API client
│   ├── wifi_manager.h      # WiFi + captive portal
│   ├── power_monitor.h     # Battery/power monitoring
│   ├── web_dashboard.h     # V380-style web UI
│   ├── ussd_handler.h      # USSD balance checking
│   └── ota_updater.h       # OTA firmware updates
├── src/
│   ├── main.cpp            # Entry point, boot, sync loop
│   ├── storage.cpp         # LittleFS implementation
│   ├── led_status.cpp      # LED patterns
│   ├── modem_manager.cpp   # GSM modem AT commands
│   ├── sim_manager.cpp     # Multi-SIM health & routing
│   ├── sms_engine.cpp      # Send queue & DLR tracking
│   ├── api_client.cpp      # HTTP API client
│   ├── wifi_manager.cpp    # WiFi management
│   ├── power_monitor.cpp   # Power/battery monitoring
│   ├── web_dashboard.cpp   # Web dashboard server
│   ├── ussd_handler.cpp    # USSD balance handler
│   └── ota_updater.cpp     # ArduinoOTA handler
├── data/
│   └── portal.html         # Captive portal UI
└── .gitignore
```

## 📜 License

Proprietary — Range View Technologies. All rights reserved.
