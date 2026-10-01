// ============================================================================
// modem_manager.h — Low-level GSM modem AT command interface
// ============================================================================
#pragma once

#include <Arduino.h>
#include <vector>

// Result of an AT command
struct AtResult {
    bool    ok;          // True if response contained "OK"
    String  response;    // Full response text
    int     errorCode;   // CME/CMS error code (0 if none)
    String  errorMsg;    // Human-readable error
};

// SMS send result
struct SmsSendResult {
    bool    success;
    uint8_t messageRef;  // +CMGS: <mr> reference number for DLR tracking
    String  errorCode;
    String  errorMsg;
};

// Network registration status
enum class NetRegStatus : uint8_t {
    NOT_REGISTERED = 0,
    REGISTERED_HOME = 1,
    SEARCHING = 2,
    DENIED = 3,
    UNKNOWN = 4,
    REGISTERED_ROAMING = 5
};

// SIM module information
struct ModemInfo {
    bool         present;       // Module detected on UART
    String       imei;
    String       iccid;        // SIM card ID
    String       operatorName;
    String       phoneNumber;
    String       networkType;
    String       registrationStatus; // added for dashboard mapping
    String       ussdBalance;
    int8_t       signalRSSI;   // Raw CSQ value (0-31, 99=unknown)
    int16_t      signalDbm;    // Converted to dBm
    uint8_t      signalBars;   // 0-5
    NetRegStatus netStatus;
    bool         simReady;     // SIM card inserted and unlocked
};

// Delivery status report from +CDS
struct DeliveryReport {
    uint8_t messageRef;     // Reference number matching SmsSendResult
    String  recipient;      // Phone number
    bool    delivered;       // True if successfully delivered
    String  timestamp;      // Delivery timestamp
    uint8_t status;         // TP-Status byte
};

// Incoming SMS
struct IncomingSms {
    String from;
    String to;
    String message;
    String timestamp;
};

class ModemManager {
public:
    // Initialize with a HardwareSerial port
    bool begin(HardwareSerial *serial, int rxPin, int txPin, uint32_t baud);

    // Check if modem is present and responding
    bool probe();

    // Full initialization sequence (after probe succeeds)
    bool initialize();

    // ─── AT Commands ────────────────────────────────────────────────────
    AtResult sendAT(const String &cmd, uint32_t timeoutMs = AT_COMMAND_TIMEOUT_MS);
    AtResult sendATWaitFor(const String &cmd, const String &expect, uint32_t timeoutMs = AT_COMMAND_TIMEOUT_MS);

    // ─── Modem Info ─────────────────────────────────────────────────────
    ModemInfo getInfo();
    int8_t   getSignalQuality();     // Returns CSQ value
    int16_t  getSignalDbm();         // Returns dBm
    String   getOperator();
    NetRegStatus getNetworkStatus();
    bool     isSIMReady();

    // ─── SMS Operations ─────────────────────────────────────────────────
    SmsSendResult sendSMS(const String &phone, const String &message);
    bool enableDeliveryReports();     // AT+CSMP to request status reports
    bool setTextMode();               // AT+CMGF=1
    bool deleteAllSMS();              // AT+CMGD=1,4

    // ─── New Features ───────────────────────────────────────────────────
    String   getPhoneNumber();
    String   getNetworkType();
    String   sendUSSD(const String &code);
    bool     sendMultipartSMS(const String &phone, const String &message);

    // ─── Unsolicited Response Handling ──────────────────────────────────
    // Call frequently to process incoming data from modem
    void processURC();

    // Check for pending delivery reports
    bool hasDeliveryReport();
    DeliveryReport getDeliveryReport();

    // Check for pending incoming SMS
    bool hasIncomingSms();
    IncomingSms getIncomingSms();

    // ─── Modem Control ──────────────────────────────────────────────────
    void reset();
    bool isReady() const { return _ready; }

private:
    HardwareSerial *_serial = nullptr;
    bool     _ready = false;
    String   _urcBuffer;

    // Pending events from URC processing
    std::vector<DeliveryReport> _pendingDLR;
    std::vector<IncomingSms>    _pendingSMS;

    // Internal helpers
    String readResponse(uint32_t timeoutMs);
    void   parseURC(const String &line);
    bool   parseCDS(const String &pdu);
    bool   parseCMT(const String &header, const String &body);
    bool   readAndParseSMS(int index);
};
