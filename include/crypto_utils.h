#pragma once
#include <Arduino.h>

namespace CryptoUtils {
    // Encrypts a JSON payload using AES-256-CBC and HMAC-SHA256
    // Returns format: iv_hex:mac_hex:ciphertext_hex
    String encryptE2EE(const String &payload, const String &secret);
    
    // Decrypts a payload from format: iv_hex:mac_hex:ciphertext_hex
    // Returns the original JSON string, or empty string on failure.
    String decryptE2EE(const String &encrypted, const String &secret);
}
