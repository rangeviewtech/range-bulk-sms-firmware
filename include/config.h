// ============================================================================
// Range View Technologies - ESP32 SMS Gateway
// config.h — Pin definitions, constants, and compile-time configuration
// ============================================================================
#pragma once

#include <Arduino.h>

// ─── Firmware Version ───────────────────────────────────────────────────────
#define FW_VERSION          "1.0.0"
#define FW_HARDWARE_MODEL   "ESP32-GSM-GW"
#define FW_OS_VERSION       "ESP32-Arduino"

// ─── SIM Module UART Pins ───────────────────────────────────────────────────
// ESP32 has 3 hardware UARTs. UART0 is used for Serial debug.
// We use UART1 and UART2 for the first two SIM modules.
// Additional SIMs (3-10) are addressed via SC16IS752 I2C-to-UART bridge
// or software serial (not recommended for production).

// SIM Slot 0: Hardware UART1
#define SIM0_RX_PIN         16
#define SIM0_TX_PIN         17
#define SIM0_BAUD           115200

// SIM Slot 1: Hardware UART2
#define SIM1_RX_PIN         26
#define SIM1_TX_PIN         27
#define SIM1_BAUD           115200

// SIM Slot 2-9: SC16IS752 I2C UART expander (optional)
// I2C address 0x48-0x4F (up to 8 channels from 4 x SC16IS752 boards)
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define SC16IS752_BASE_ADDR 0x48  // First expander I2C address

// Maximum supported SIM slots
#define MAX_SIM_SLOTS       10

// ─── LED Status Pin ─────────────────────────────────────────────────────────
#define STATUS_LED_PIN      2   // Built-in LED on most ESP32 dev boards

// ─── Modem Reset Pin (optional, active LOW) ─────────────────────────────────
#define MODEM_RST_PIN       4   // Connect to all modem RST pins via transistor

// --- Battery/Power Monitoring ---
#define BATTERY_ADC_PIN     34    // ADC1 pin for voltage divider
#define BATTERY_R1          100000.0  // Top resistor (100kΩ)
#define BATTERY_R2          100000.0  // Bottom resistor (100kΩ)
#define USB_DETECT_PIN      35    // HIGH when USB 5V present

// --- Ethernet (W5500 SPI, optional) ---
#define ETH_ENABLED         false   // Set true if W5500 wired
#define ETH_CS_PIN          5
#define ETH_MOSI_PIN        23
#define ETH_MISO_PIN        19
#define ETH_SCLK_PIN        18
#define ETH_INT_PIN         -1    // Not used
#define ETH_RST_PIN         -1    // Not used

// --- Factory Reset Button ---
#define FACTORY_RESET_PIN   0     // BOOT button on most ESP32 boards
#define FACTORY_RESET_HOLD_MS 5000 // Hold 5 seconds to factory reset

// --- Web Dashboard ---
#define WEB_DASHBOARD_PORT  80
#define WEB_DASHBOARD_ENABLED true

// --- USSD ---
#define USSD_BALANCE_CODE   "*123#"  // Operator balance check code
#define USSD_POLL_INTERVAL_MS 3600000 // Check balance every hour

// --- Multi-part SMS ---
#define MAX_SMS_PARTS       4     // Max concatenated SMS parts
#define SMS_PART_LENGTH     153   // Characters per part (GSM7 concat)

// --- Temperature ---
#define TEMP_WARNING_C      70.0  // Warn if internal temp exceeds this

// ─── Timing Constants (milliseconds) ────────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS       60000   // 60 seconds
#define QUEUE_POLL_IDLE_MS          15000   // 15 seconds when no jobs
#define QUEUE_POLL_BUSY_MS          5000    // 5 seconds when busy sending
#define AT_COMMAND_TIMEOUT_MS       5000    // Default AT command timeout
#define AT_SEND_TIMEOUT_MS          30000   // SMS send timeout (AT+CMGS)
#define WIFI_RECONNECT_MIN_MS       1000    // Min WiFi reconnect delay
#define WIFI_RECONNECT_MAX_MS       60000   // Max WiFi reconnect delay
#define MODEM_INIT_DELAY_MS         3000    // Wait after modem power-on
#define BLACKLIST_WINDOW_MS         600000  // 10 minutes failure window
#define BLACKLIST_RECOVERY_MS       300000  // 5 minutes between recovery probes
#define WATCHDOG_TIMEOUT_S          30      // Watchdog timeout in seconds
#define FOTA_CHECK_INTERVAL_MS      86400000 // 24 hours

// ─── Thresholds ─────────────────────────────────────────────────────────────
#define BLACKLIST_FAIL_THRESHOLD    5       // Failures within window to blacklist
#define MAX_AT_RETRIES              3       // Max retries for failed AT commands
#define MAX_SMS_PER_POLL            10      // Max SMS to claim per queue poll
#define LOW_HEAP_THRESHOLD          20000   // Bytes — warn if free heap below this
#define SMS_EXPIRE_BUFFER_S         30      // Don't attempt SMS expiring within 30s

// ─── LittleFS File Paths ────────────────────────────────────────────────────
#define CONFIG_FILE         "/config.json"
#define QUEUE_FILE          "/queue.json"
#define SIM_STATUS_FILE     "/sim_status.json"
#define STATS_FILE          "/stats.json"

// ─── API Defaults ───────────────────────────────────────────────────────────
#define DEFAULT_API_BASE    ""
// Enable plain HTTP only in explicitly local development builds. Production
// devices must validate HTTPS with the configured backend root CA.
#ifndef ALLOW_INSECURE_HTTP_DEV
#define ALLOW_INSECURE_HTTP_DEV 0
#endif
#define API_REGISTER        "/device/gateways/register"
#define API_HEARTBEAT       "/device/gateways/heartbeat"
#define API_QUEUE           "/device/gateways/queue"
#define API_RESULT          "/device/gateways/messages/result"
#define API_INCOMING        "/device/gateways/messages/incoming"
#define API_FIRMWARE_CHECK  "/device/gateways/firmware/check"

// ─── WiFi AP for Provisioning ───────────────────────────────────────────────
#define AP_SSID_PREFIX      "RangeGW-"
#define AP_PASSWORD         ""  // Open AP for easy setup (secured by pairing code)

// ─── OTA ────────────────────────────────────────────────────────────────────
#define OTA_HOSTNAME_PREFIX "rangegw-"

// ─── Log Levels ─────────────────────────────────────────────────────────────
enum LogLevel : uint8_t {
    LOG_NONE  = 0,
    LOG_ERROR = 1,
    LOG_WARN  = 2,
    LOG_INFO  = 3,
    LOG_DEBUG = 4,
    LOG_TRACE = 5
};

// Current compile-time log level
#define CURRENT_LOG_LEVEL   LOG_DEBUG

// Log macros
#define LOG_E(tag, fmt, ...) if (CURRENT_LOG_LEVEL >= LOG_ERROR) Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_W(tag, fmt, ...) if (CURRENT_LOG_LEVEL >= LOG_WARN)  Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_I(tag, fmt, ...) if (CURRENT_LOG_LEVEL >= LOG_INFO)  Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_D(tag, fmt, ...) if (CURRENT_LOG_LEVEL >= LOG_DEBUG) Serial.printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define LOG_T(tag, fmt, ...) if (CURRENT_LOG_LEVEL >= LOG_TRACE) Serial.printf("[T][%s] " fmt "\n", tag, ##__VA_ARGS__)
