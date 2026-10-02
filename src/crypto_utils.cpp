#include "crypto_utils.h"
#include <mbedtls/md.h>
#include <mbedtls/aes.h>
#include <esp_system.h>

static void toHex(const uint8_t *in, size_t len, String &out) {
    const char hexChars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out += hexChars[(in[i] >> 4) & 0x0F];
        out += hexChars[in[i] & 0x0F];
    }
}

static bool fromHex(const String &hex, uint8_t *out, size_t maxLen) {
    if (hex.length() % 2 != 0 || hex.length() / 2 > maxLen) return false;
    for (size_t i = 0; i < hex.length(); i += 2) {
        char c1 = hex[i];
        char c2 = hex[i+1];
        uint8_t v1 = (c1 >= '0' && c1 <= '9') ? (c1 - '0') : (c1 >= 'a' && c1 <= 'f') ? (c1 - 'a' + 10) : (c1 >= 'A' && c1 <= 'F') ? (c1 - 'A' + 10) : 0;
        uint8_t v2 = (c2 >= '0' && c2 <= '9') ? (c2 - '0') : (c2 >= 'a' && c2 <= 'f') ? (c2 - 'a' + 10) : (c2 >= 'A' && c2 <= 'F') ? (c2 - 'A' + 10) : 0;
        out[i/2] = (v1 << 4) | v2;
    }
    return true;
}

static void deriveKey(const String &secret, const char *suffix, uint8_t *keyOut) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    
    String combined = secret + suffix;
    mbedtls_md_update(&ctx, (const unsigned char *)combined.c_str(), combined.length());
    mbedtls_md_finish(&ctx, keyOut);
    mbedtls_md_free(&ctx);
}

String CryptoUtils::encryptE2EE(const String &payload, const String &secret) {
    uint8_t encKey[32];
    uint8_t macKey[32];
    deriveKey(secret, "_E2EE_KEY", encKey);
    deriveKey(secret, "_E2EE_MAC", macKey);

    uint8_t iv[16];
    for (int i = 0; i < 16; i++) iv[i] = esp_random() & 0xFF;

    size_t dataLen = payload.length();
    size_t padLen = 16 - (dataLen % 16);
    size_t paddedLen = dataLen + padLen;
    uint8_t *padded = (uint8_t *)malloc(paddedLen);
    if (!padded) return "";
    
    memcpy(padded, payload.c_str(), dataLen);
    for (size_t i = dataLen; i < paddedLen; i++) padded[i] = padLen; // PKCS7

    uint8_t *ciphertext = (uint8_t *)malloc(paddedLen);
    if (!ciphertext) { free(padded); return ""; }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, encKey, 256);
    
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv_copy, padded, ciphertext);
    mbedtls_aes_free(&aes);
    free(padded);

    String ivHex = "";
    toHex(iv, 16, ivHex);

    String cipherHex = "";
    toHex(ciphertext, paddedLen, cipherHex);
    free(ciphertext);

    String macInput = ivHex + ":" + cipherHex;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1); // 1 = HMAC
    mbedtls_md_hmac_starts(&ctx, macKey, 32);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)macInput.c_str(), macInput.length());
    
    uint8_t mac[32];
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);

    String macHex = "";
    toHex(mac, 32, macHex);

    return ivHex + ":" + macHex + ":" + cipherHex;
}

String CryptoUtils::decryptE2EE(const String &encrypted, const String &secret) {
    int firstColon = encrypted.indexOf(':');
    int secondColon = encrypted.indexOf(':', firstColon + 1);
    if (firstColon < 0 || secondColon < 0) return "";

    String ivHex = encrypted.substring(0, firstColon);
    String macHex = encrypted.substring(firstColon + 1, secondColon);
    String cipherHex = encrypted.substring(secondColon + 1);

    uint8_t encKey[32];
    uint8_t macKey[32];
    deriveKey(secret, "_E2EE_KEY", encKey);
    deriveKey(secret, "_E2EE_MAC", macKey);

    String macInput = ivHex + ":" + cipherHex;
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, macKey, 32);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)macInput.c_str(), macInput.length());
    
    uint8_t expectedMac[32];
    mbedtls_md_hmac_finish(&ctx, expectedMac);
    mbedtls_md_free(&ctx);

    String expectedMacHex = "";
    toHex(expectedMac, 32, expectedMacHex);

    if (macHex != expectedMacHex) return "";

    uint8_t iv[16];
    if (!fromHex(ivHex, iv, 16)) return "";

    size_t cipherLen = cipherHex.length() / 2;
    if (cipherLen == 0 || cipherLen % 16 != 0) return "";

    uint8_t *ciphertext = (uint8_t *)malloc(cipherLen);
    if (!ciphertext) return "";
    if (!fromHex(cipherHex, ciphertext, cipherLen)) { free(ciphertext); return ""; }

    uint8_t *plaintext = (uint8_t *)malloc(cipherLen + 1);
    if (!plaintext) { free(ciphertext); return ""; }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, encKey, 256);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipherLen, iv, ciphertext, plaintext);
    mbedtls_aes_free(&aes);
    free(ciphertext);

    uint8_t padLen = plaintext[cipherLen - 1];
    if (padLen > 16 || padLen == 0 || padLen > cipherLen) {
        free(plaintext);
        return "";
    }

    plaintext[cipherLen - padLen] = 0;
    String result = (char *)plaintext;
    free(plaintext);

    return result;
}
