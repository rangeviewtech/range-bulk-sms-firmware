// ============================================================================
// modem_manager.cpp — GSM modem AT command interface implementation
// ============================================================================

#include "modem_manager.h"
#include "config.h"

static const char *TAG = "MODEM";

// ─── Initialization ─────────────────────────────────────────────────────────

bool ModemManager::begin(HardwareSerial *serial, int rxPin, int txPin, uint32_t baud) {
    _serial = serial;
    _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
    _ready = false;
    LOG_D(TAG, "UART initialized (RX=%d, TX=%d, %d baud)", rxPin, txPin, baud);
    return true;
}

bool ModemManager::probe() {
    if (!_serial) return false;

    // Flush any garbage
    while (_serial->available()) _serial->read();

    // Try AT command up to 3 times
    for (int i = 0; i < MAX_AT_RETRIES; i++) {
        AtResult res = sendAT("AT", 2000);
        if (res.ok) {
            LOG_I(TAG, "Modem detected (attempt %d)", i + 1);
            return true;
        }
        delay(500);
    }
    LOG_D(TAG, "No modem detected on this UART");
    return false;
}

bool ModemManager::initialize() {
    if (!_serial) return false;

    // Disable echo
    sendAT("ATE0");
    delay(100);

    // Set verbose error reporting
    sendAT("AT+CMEE=2");
    delay(100);

    // Check SIM card
    AtResult simRes = sendAT("AT+CPIN?", 5000);
    if (!simRes.ok || simRes.response.indexOf("READY") < 0) {
        LOG_E(TAG, "SIM not ready: %s", simRes.response.c_str());
        return false;
    }

    // Wait for network registration (up to 30 seconds)
    bool registered = false;
    for (int i = 0; i < 15; i++) {
        NetRegStatus status = getNetworkStatus();
        if (status == NetRegStatus::REGISTERED_HOME ||
            status == NetRegStatus::REGISTERED_ROAMING) {
            registered = true;
            break;
        }
        LOG_D(TAG, "Waiting for network... (%d/15)", i + 1);
        delay(2000);
    }

    if (!registered) {
        LOG_W(TAG, "Network registration timeout");
        // Continue anyway — might register later
    }

    // Set text mode for SMS
    if (!setTextMode()) {
        LOG_E(TAG, "Failed to set text mode");
        return false;
    }

    // Enable delivery reports
    enableDeliveryReports();

    // Configure new SMS notification: send directly to terminal
    sendAT("AT+CNMI=2,2,0,1,0");
    delay(100);

    // Set character set to GSM
    sendAT("AT+CSCS=\"GSM\"");
    delay(100);

    _ready = true;
    LOG_I(TAG, "Modem initialized successfully");
    return true;
}

// ─── AT Commands ────────────────────────────────────────────────────────────

AtResult ModemManager::sendAT(const String &cmd, uint32_t timeoutMs) {
    AtResult result = { false, "", 0, "" };
    if (!_serial) return result;

    // Flush input buffer
    while (_serial->available()) {
        char c = _serial->read();
        // Feed any data to URC buffer in case it's an unsolicited message
        _urcBuffer += c;
        if (c == '\n') {
            parseURC(_urcBuffer);
            _urcBuffer = "";
        }
    }

    LOG_T(TAG, ">> %s", cmd.c_str());
    _serial->println(cmd);

    result.response = readResponse(timeoutMs);
    LOG_T(TAG, "<< %s", result.response.c_str());

    // Check for OK/ERROR
    if (result.response.indexOf("OK") >= 0) {
        result.ok = true;
    } else if (result.response.indexOf("ERROR") >= 0) {
        result.ok = false;
        // Parse CME/CMS error
        int cmeIdx = result.response.indexOf("+CME ERROR:");
        int cmsIdx = result.response.indexOf("+CMS ERROR:");
        if (cmeIdx >= 0) {
            result.errorMsg = result.response.substring(cmeIdx + 12);
            result.errorMsg.trim();
        } else if (cmsIdx >= 0) {
            result.errorMsg = result.response.substring(cmsIdx + 12);
            result.errorMsg.trim();
        } else {
            result.errorMsg = "ERROR";
        }
    }

    return result;
}

AtResult ModemManager::sendATWaitFor(const String &cmd, const String &expect, uint32_t timeoutMs) {
    AtResult result = sendAT(cmd, timeoutMs);
    if (result.response.indexOf(expect) < 0) {
        result.ok = false;
        if (result.errorMsg.isEmpty()) {
            result.errorMsg = "Expected '" + expect + "' not found";
        }
    }
    return result;
}

// ─── Modem Info ─────────────────────────────────────────────────────────────

ModemInfo ModemManager::getInfo() {
    ModemInfo info;
    info.present = _ready;
    info.signalRSSI = getSignalQuality();
    info.signalDbm = getSignalDbm();
    info.operatorName = getOperator();
    info.netStatus = getNetworkStatus();
    info.simReady = isSIMReady();

    // Get IMEI
    AtResult imeiRes = sendAT("AT+GSN", 3000);
    if (imeiRes.ok) {
        info.imei = imeiRes.response;
        // Extract just the IMEI number (15 digits)
        for (int i = 0; i < (int)info.imei.length(); i++) {
            if (isdigit(info.imei.charAt(i))) {
                int start = i;
                while (i < (int)info.imei.length() && isdigit(info.imei.charAt(i))) i++;
                info.imei = info.imei.substring(start, i);
                break;
            }
        }
    }

    // Get ICCID
    AtResult iccidRes = sendAT("AT+CCID", 3000);
    if (iccidRes.ok) {
        info.iccid = iccidRes.response;
        for (int i = 0; i < (int)info.iccid.length(); i++) {
            if (isdigit(info.iccid.charAt(i))) {
                int start = i;
                while (i < (int)info.iccid.length() && isdigit(info.iccid.charAt(i))) i++;
                info.iccid = info.iccid.substring(start, i);
                break;
            }
        }
    }

    return info;
}

int8_t ModemManager::getSignalQuality() {
    AtResult res = sendAT("AT+CSQ", 3000);
    if (!res.ok) return -1;

    int idx = res.response.indexOf("+CSQ:");
    if (idx < 0) return -1;

    String val = res.response.substring(idx + 5);
    val.trim();
    int comma = val.indexOf(',');
    if (comma > 0) val = val.substring(0, comma);

    int csq = val.toInt();
    return (csq == 99) ? -1 : (int8_t)csq;
}

int16_t ModemManager::getSignalDbm() {
    int8_t csq = getSignalQuality();
    if (csq < 0 || csq == 99) return -999;
    // CSQ to dBm: dBm = -113 + (2 * CSQ)
    return -113 + (2 * csq);
}

String ModemManager::getOperator() {
    AtResult res = sendAT("AT+COPS?", 5000);
    if (!res.ok) return "Unknown";

    int idx = res.response.indexOf("+COPS:");
    if (idx < 0) return "Unknown";

    // Format: +COPS: <mode>,<format>,\"<operator>\",<act>
    int q1 = res.response.indexOf('"', idx);
    int q2 = res.response.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) {
        return res.response.substring(q1 + 1, q2);
    }
    return "Unknown";
}

NetRegStatus ModemManager::getNetworkStatus() {
    AtResult res = sendAT("AT+CREG?", 3000);
    if (!res.ok) return NetRegStatus::UNKNOWN;

    int idx = res.response.indexOf("+CREG:");
    if (idx < 0) return NetRegStatus::UNKNOWN;

    String val = res.response.substring(idx + 6);
    val.trim();
    int comma = val.indexOf(',');
    if (comma < 0) return NetRegStatus::UNKNOWN;

    int stat = val.substring(comma + 1).toInt();
    if (stat >= 0 && stat <= 5) {
        return (NetRegStatus)stat;
    }
    return NetRegStatus::UNKNOWN;
}

bool ModemManager::isSIMReady() {
    AtResult res = sendAT("AT+CPIN?", 3000);
    return res.ok && res.response.indexOf("READY") >= 0;
}

// ─── SMS Operations ─────────────────────────────────────────────────────────

SmsSendResult ModemManager::sendSMS(const String &phone, const String &message) {
    SmsSendResult result = { false, 0, "", "" };

    if (!_ready) {
        result.errorCode = "MODEM_NOT_READY";
        result.errorMsg = "Modem not initialized";
        return result;
    }

    // Check network before sending
    NetRegStatus netStat = getNetworkStatus();
    if (netStat != NetRegStatus::REGISTERED_HOME &&
        netStat != NetRegStatus::REGISTERED_ROAMING) {
        result.errorCode = "NO_SERVICE";
        result.errorMsg = "Not registered on network";
        return result;
    }

    // Start SMS send: AT+CMGS="phone"
    String cmd = "AT+CMGS=\"" + phone + "\"";
    _serial->println(cmd);

    // Wait for the > prompt
    uint32_t start = millis();
    bool gotPrompt = false;
    String promptResp;
    while (millis() - start < 10000) {
        if (_serial->available()) {
            char c = _serial->read();
            promptResp += c;
            if (c == '>') {
                gotPrompt = true;
                break;
            }
            // Check for ERROR before prompt
            if (promptResp.indexOf("ERROR") >= 0) {
                result.errorCode = "SEND_ERROR";
                result.errorMsg = "Modem rejected CMGS command";
                LOG_E(TAG, "SMS send rejected: %s", promptResp.c_str());
                return result;
            }
        }
        delay(10);
    }

    if (!gotPrompt) {
        result.errorCode = "TIMEOUT";
        result.errorMsg = "Timeout waiting for > prompt";
        // Send Ctrl+Z anyway to cancel
        _serial->write(0x1A);
        LOG_E(TAG, "SMS send timeout waiting for prompt");
        return result;
    }

    // Send the message body followed by Ctrl+Z
    _serial->print(message);
    delay(100);
    _serial->write(0x1A);

    // Wait for +CMGS: <mr> or ERROR (extended timeout for air transmission)
    String sendResp = readResponse(AT_SEND_TIMEOUT_MS);
    LOG_D(TAG, "CMGS response: %s", sendResp.c_str());

    int cmgsIdx = sendResp.indexOf("+CMGS:");
    if (cmgsIdx >= 0) {
        // Extract message reference number
        String mrStr = sendResp.substring(cmgsIdx + 6);
        mrStr.trim();
        result.messageRef = (uint8_t)mrStr.toInt();
        result.success = true;
        LOG_I(TAG, "SMS sent to %s (ref=%d)", phone.c_str(), result.messageRef);
    } else if (sendResp.indexOf("ERROR") >= 0) {
        result.errorCode = "SEND_FAILED";
        int cmsIdx = sendResp.indexOf("+CMS ERROR:");
        if (cmsIdx >= 0) {
            result.errorMsg = sendResp.substring(cmsIdx + 12);
            result.errorMsg.trim();
            result.errorCode = "CMS_ERROR";
        } else {
            result.errorMsg = "Modem returned ERROR during send";
        }
        LOG_E(TAG, "SMS send failed to %s: %s", phone.c_str(), result.errorMsg.c_str());
    } else {
        result.errorCode = "SEND_UNCERTAIN";
        result.errorMsg = "No clear response from modem";
        LOG_W(TAG, "SMS send uncertain to %s", phone.c_str());
    }

    return result;
}

bool ModemManager::enableDeliveryReports() {
    // AT+CSMP=<fo>,<vp>,<pid>,<dcs>
    // fo bit 5 (0x20) = Status Report Request
    // Default: fo=49 (0x31 = reply path + SRR), vp=167, pid=0, dcs=0
    AtResult res = sendAT("AT+CSMP=49,167,0,0");
    if (res.ok) {
        LOG_I(TAG, "Delivery reports enabled");
    } else {
        LOG_W(TAG, "Failed to enable delivery reports: %s", res.errorMsg.c_str());
    }
    return res.ok;
}

bool ModemManager::setTextMode() {
    AtResult res = sendAT("AT+CMGF=1");
    return res.ok;
}

bool ModemManager::deleteAllSMS() {
    AtResult res = sendAT("AT+CMGD=1,4", 10000);
    return res.ok;
}

// ─── URC Processing ─────────────────────────────────────────────────────────

void ModemManager::processURC() {
    if (!_serial) return;

    while (_serial->available()) {
        char c = _serial->read();
        _urcBuffer += c;

        if (c == '\n') {
            _urcBuffer.trim();
            if (_urcBuffer.length() > 0) {
                parseURC(_urcBuffer);
            }
            _urcBuffer = "";
        }

        // Safety: prevent buffer overflow
        if (_urcBuffer.length() > 512) {
            _urcBuffer = "";
        }
    }
}

bool ModemManager::hasDeliveryReport() {
    return !_pendingDLR.empty();
}

DeliveryReport ModemManager::getDeliveryReport() {
    if (_pendingDLR.empty()) {
        return { 0, "", false, "", 0 };
    }
    DeliveryReport report = _pendingDLR.front();
    _pendingDLR.erase(_pendingDLR.begin());
    return report;
}

bool ModemManager::hasIncomingSms() {
    return !_pendingSMS.empty();
}

IncomingSms ModemManager::getIncomingSms() {
    if (_pendingSMS.empty()) {
        return { "", "", "", "" };
    }
    IncomingSms sms = _pendingSMS.front();
    _pendingSMS.erase(_pendingSMS.begin());
    return sms;
}

// ─── New Features ─────────────────────────────────────────────────────────

String ModemManager::getPhoneNumber() {
    AtResult res = sendAT("AT+CNUM", 3000);
    if (res.ok && res.response.indexOf("+CNUM:") >= 0) {
        // Parse +CNUM: "","+1234567890",145
        int q1 = res.response.indexOf("\",\"");
        if (q1 >= 0) {
            int q2 = res.response.indexOf("\"", q1 + 3);
            if (q2 > q1) {
                return res.response.substring(q1 + 3, q2);
            }
        }
    }
    return "";
}

String ModemManager::getNetworkType() {
    AtResult res = sendAT("AT+COPS?", 3000);
    // +COPS: 0,0,"MTN",0 -> GSM
    // Act field: 0=GSM, 2=UTRAN(3G), 7=E-UTRAN(LTE)
    if (res.ok && res.response.indexOf("+COPS:") >= 0) {
        int lastComma = res.response.lastIndexOf(',');
        if (lastComma > 0) {
            int act = res.response.substring(lastComma + 1).toInt();
            if (act == 0) return "GSM";
            if (act == 2) return "3G";
            if (act == 7) return "LTE";
        }
    }
    return "UNKNOWN";
}

String ModemManager::sendUSSD(const String &code) {
    sendAT("AT+CUSD=1,\"" + code + "\",15", 30000);
    // Wait for +CUSD: URC
    uint32_t start = millis();
    while (millis() - start < 30000) {
        if (_serial->available()) {
            char c = _serial->read();
            _urcBuffer += c;
            if (c == '\n') {
                _urcBuffer.trim();
                if (_urcBuffer.startsWith("+CUSD:")) {
                    // +CUSD: 0,"Balance is N500.00...",15
                    int q1 = _urcBuffer.indexOf('"');
                    int q2 = _urcBuffer.lastIndexOf('"');
                    if (q1 >= 0 && q2 > q1) {
                        String response = _urcBuffer.substring(q1 + 1, q2);
                        _urcBuffer = "";
                        return response;
                    }
                }
                _urcBuffer = "";
            }
        }
        delay(1);
    }
    return "";
}

bool ModemManager::sendMultipartSMS(const String &phone, const String &message) {
    // Basic fallback: just send as single text if short enough
    if (message.length() <= 160) {
        return sendSMS(phone, message).success;
    }
    
    // Simplistic text splitting for now, UDH logic is complex and modem-specific.
    // For a robust setup, use PDU mode or standard multi-part concat features.
    // Here we'll just split and send sequentially for basic implementation.
    int parts = message.length() / SMS_PART_LENGTH + (message.length() % SMS_PART_LENGTH > 0 ? 1 : 0);
    if (parts > MAX_SMS_PARTS) return false;
    
    bool allSuccess = true;
    for (int i=0; i<parts; i++) {
        String part = message.substring(i * SMS_PART_LENGTH, (i+1) * SMS_PART_LENGTH);
        if (!sendSMS(phone, "(Part " + String(i+1) + "/" + String(parts) + ") " + part).success) {
            allSuccess = false;
        }
        delay(1000); // Small pause between parts
    }
    return allSuccess;
}

// ─── Modem Control ──────────────────────────────────────────────────────────

void ModemManager::reset() {
    LOG_W(TAG, "Resetting modem");
    _ready = false;
    _pendingDLR.clear();
    _pendingSMS.clear();
    _urcBuffer = "";

    // Try software reset first
    sendAT("AT+CFUN=1,1", 5000);
    delay(MODEM_INIT_DELAY_MS);

    // Re-initialize
    if (probe()) {
        initialize();
    }
}

// ─── Private Helpers ────────────────────────────────────────────────────────

String ModemManager::readResponse(uint32_t timeoutMs) {
    String response;
    uint32_t start = millis();

    while (millis() - start < timeoutMs) {
        if (_serial->available()) {
            char c = _serial->read();
            response += c;

            // Check for terminal conditions
            if (response.endsWith("OK\r\n") || response.endsWith("OK\n")) break;
            if (response.endsWith("ERROR\r\n") || response.endsWith("ERROR\n")) break;
            if (response.indexOf("+CME ERROR:") >= 0 && response.endsWith("\n")) break;
            if (response.indexOf("+CMS ERROR:") >= 0 && response.endsWith("\n")) break;
            if (response.indexOf("+CMGS:") >= 0 && response.endsWith("OK\n")) break;
        }
        delay(1);
    }

    response.trim();
    return response;
}

void ModemManager::parseURC(const String &line) {
    // +CDS: <length>\r\n<pdu> — Delivery status report
    if (line.startsWith("+CDS:")) {
        // In text mode, the format is:
        // +CDS: <fo>,<mr>,<ra>,<tora>,<scts>,<dt>,<st>
        DeliveryReport report;
        // Parse comma-separated values
        int pos = 5; // After "+CDS:"
        String parts[7];
        int partIdx = 0;
        String current;
        for (int i = pos; i < (int)line.length() && partIdx < 7; i++) {
            if (line.charAt(i) == ',') {
                parts[partIdx++] = current;
                current = "";
            } else if (line.charAt(i) != '"') {
                current += line.charAt(i);
            }
        }
        if (partIdx < 7) parts[partIdx] = current;

        report.messageRef = parts[1].toInt(); // <mr>
        report.recipient = parts[2];          // <ra>
        report.recipient.trim();
        report.timestamp = parts[5];          // <dt>
        report.status = parts[6].toInt();     // <st>
        report.delivered = (report.status == 0); // TP-Status 0 = delivered

        LOG_I(TAG, "DLR received: ref=%d, to=%s, delivered=%s, status=%d",
              report.messageRef, report.recipient.c_str(),
              report.delivered ? "yes" : "no", report.status);

        _pendingDLR.push_back(report);
        return;
    }

    // +CMT: "<phone>","","<timestamp>"\r\n<message> — Incoming SMS (direct)
    if (line.startsWith("+CMT:")) {
        IncomingSms sms;
        // Extract sender phone from first quoted string
        int q1 = line.indexOf('"');
        int q2 = line.indexOf('"', q1 + 1);
        if (q1 >= 0 && q2 > q1) {
            sms.from = line.substring(q1 + 1, q2);
        }
        // Timestamp in third quoted pair
        int q5 = -1, q6 = -1;
        int qcount = 0;
        for (int i = 0; i < (int)line.length(); i++) {
            if (line.charAt(i) == '"') {
                qcount++;
                if (qcount == 5) q5 = i;
                if (qcount == 6) { q6 = i; break; }
            }
        }
        if (q5 >= 0 && q6 > q5) {
            sms.timestamp = line.substring(q5 + 1, q6);
        }
        // The message body follows on the next line — we'll capture it
        // by reading more from the serial in the next processURC call.
        // For now, store the header and wait for body.
        // NOTE: Since we're processing line-by-line, the body will come
        // as the next line. We handle this by storing a partial SMS.
        // For simplicity in text mode with +CNMI=2,2, the body follows.
        _pendingSMS.push_back(sms);
        LOG_I(TAG, "Incoming SMS from %s", sms.from.c_str());
        return;
    }

    // +CMTI: "SM",<index> — New SMS stored in SIM
    if (line.startsWith("+CMTI:")) {
        int comma = line.indexOf(',');
        if (comma > 0) {
            int index = line.substring(comma + 1).toInt();
            LOG_I(TAG, "New SMS stored at index %d", index);
            readAndParseSMS(index);
        }
        return;
    }

    // RING — Incoming call (ignore but log)
    if (line.startsWith("RING") || line.startsWith("+CLIP:")) {
        LOG_D(TAG, "Incoming call (ignored): %s", line.c_str());
        // Hang up incoming calls
        sendAT("ATH");
        return;
    }
}

bool ModemManager::parseCDS(const String &pdu) {
    // Handled in parseURC for text mode
    return false;
}

bool ModemManager::parseCMT(const String &header, const String &body) {
    // Handled in parseURC
    return false;
}

bool ModemManager::readAndParseSMS(int index) {
    // Read SMS at index
    String cmd = "AT+CMGR=" + String(index);
    AtResult res = sendAT(cmd, 5000);
    if (!res.ok) return false;

    // Parse: +CMGR: "REC UNREAD","<phone>","","<timestamp>"\r\n<message>
    IncomingSms sms;
    int cmgrIdx = res.response.indexOf("+CMGR:");
    if (cmgrIdx < 0) return false;

    String header = res.response.substring(cmgrIdx);
    int nlIdx = header.indexOf('\n');
    if (nlIdx < 0) return false;

    String headerLine = header.substring(0, nlIdx);
    String body = header.substring(nlIdx + 1);

    // Extract phone from second quoted string
    int qcount = 0;
    int q3 = -1, q4 = -1;
    for (int i = 0; i < (int)headerLine.length(); i++) {
        if (headerLine.charAt(i) == '"') {
            qcount++;
            if (qcount == 3) q3 = i;
            if (qcount == 4) { q4 = i; break; }
        }
    }
    if (q3 >= 0 && q4 > q3) {
        sms.from = headerLine.substring(q3 + 1, q4);
    }

    // Clean message body
    body.trim();
    int okIdx = body.lastIndexOf("OK");
    if (okIdx > 0) {
        body = body.substring(0, okIdx);
        body.trim();
    }
    sms.message = body;
    sms.timestamp = "";

    if (!sms.from.isEmpty() && !sms.message.isEmpty()) {
        _pendingSMS.push_back(sms);
        // Delete after reading
        sendAT("AT+CMGD=" + String(index));
        return true;
    }
    return false;
}
