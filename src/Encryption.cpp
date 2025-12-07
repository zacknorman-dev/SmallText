#include "Encryption.h"
#include <RNG.h>
#include <string.h>

Encryption::Encryption() {
    memset(key, 0, 32);
}

void Encryption::setKey(const uint8_t* newKey) {
    memcpy(key, newKey, 32);
}

void Encryption::generateNonce(uint8_t* nonce) {
    // Generate random nonce
    RNG.begin("SmolTxt");
    for (int i = 0; i < NONCE_SIZE; i++) {
        RNG.rand(&nonce[i], 1);
    }
}

int Encryption::encrypt(const uint8_t* plaintext, size_t plaintextLen, 
                        uint8_t* output, size_t outputMaxLen) {
    if (plaintextLen > MAX_PLAINTEXT) {
        return 0;  // Message too long
    }
    
    size_t requiredLen = NONCE_SIZE + plaintextLen + TAG_SIZE;
    if (outputMaxLen < requiredLen) {
        return 0;  // Output buffer too small
    }
    
    // Generate nonce and store it at the beginning
    uint8_t nonce[NONCE_SIZE];
    generateNonce(nonce);
    memcpy(output, nonce, NONCE_SIZE);
    
    // Derive Poly1305 key from ChaCha20 keystream (RFC 8439)
    // Generate the first 32 bytes of keystream with counter=0
    uint8_t poly1305Key[32];
    chacha.clear();
    chacha.setKey(key, 32);
    chacha.setIV(nonce, NONCE_SIZE);
    uint8_t counter[4] = {0, 0, 0, 0};
    chacha.setCounter(counter, 4);
    
    // Generate Poly1305 key by encrypting 32 zero bytes
    uint8_t zeros[32] = {0};
    chacha.encrypt(poly1305Key, zeros, 32);
    
    // Now set counter to 1 for actual encryption
    counter[0] = 1;
    chacha.setCounter(counter, 4);
    
    // Encrypt the plaintext
    uint8_t* ciphertext = output + NONCE_SIZE;
    chacha.encrypt(ciphertext, plaintext, plaintextLen);
    
    // Compute Poly1305 MAC over ciphertext
    poly1305.reset(poly1305Key);
    poly1305.update(ciphertext, plaintextLen);
    uint8_t* tag = output + NONCE_SIZE + plaintextLen;
    // Poly1305 doesn't use nonce in finalize - just the derived key
    uint8_t dummy[16] = {0};
    poly1305.finalize(dummy, tag, TAG_SIZE);
    
    return requiredLen;
}

int Encryption::decrypt(const uint8_t* input, size_t inputLen,
                        uint8_t* output, size_t outputMaxLen) {
    if (inputLen < NONCE_SIZE + TAG_SIZE) {
        return -1;  // Invalid input
    }
    
    size_t ciphertextLen = inputLen - NONCE_SIZE - TAG_SIZE;
    if (outputMaxLen < ciphertextLen) {
        return -1;  // Output buffer too small
    }
    
    // Extract nonce
    uint8_t nonce[NONCE_SIZE];
    memcpy(nonce, input, NONCE_SIZE);
    
    // Extract ciphertext and tag
    const uint8_t* ciphertext = input + NONCE_SIZE;
    const uint8_t* receivedTag = input + NONCE_SIZE + ciphertextLen;
    
    // Derive Poly1305 key from ChaCha20 keystream (RFC 8439)
    uint8_t poly1305Key[32];
    chacha.clear();
    chacha.setKey(key, 32);
    chacha.setIV(nonce, NONCE_SIZE);
    uint8_t counter[4] = {0, 0, 0, 0};
    chacha.setCounter(counter, 4);
    
    // Generate Poly1305 key
    uint8_t zeros[32] = {0};
    chacha.encrypt(poly1305Key, zeros, 32);
    
    // Verify Poly1305 MAC
    poly1305.reset(poly1305Key);
    poly1305.update(ciphertext, ciphertextLen);
    uint8_t computedTag[TAG_SIZE];
    uint8_t dummy[16] = {0};
    poly1305.finalize(dummy, computedTag, TAG_SIZE);
    
    // Constant-time comparison
    bool tagValid = true;
    for (int i = 0; i < TAG_SIZE; i++) {
        if (computedTag[i] != receivedTag[i]) {
            tagValid = false;
        }
    }
    
    if (!tagValid) {
        return -1;  // Authentication failed
    }
    
    // Set counter to 1 for decryption
    counter[0] = 1;
    chacha.setCounter(counter, 4);
    
    // Decrypt
    chacha.decrypt(output, ciphertext, ciphertextLen);
    
    return ciphertextLen;
}

bool Encryption::encryptString(const String& plaintext, uint8_t* output, 
                               size_t outputMaxLen, size_t* outputLen) {
    int len = encrypt((const uint8_t*)plaintext.c_str(), plaintext.length(), 
                      output, outputMaxLen);
    if (len > 0) {
        *outputLen = len;
        return true;
    }
    return false;
}

bool Encryption::decryptString(const uint8_t* input, size_t inputLen, String& plaintext) {
    uint8_t decrypted[MAX_PLAINTEXT + 1];
    int len = decrypt(input, inputLen, decrypted, MAX_PLAINTEXT);
    
    if (len > 0) {
        decrypted[len] = '\0';
        plaintext = String((char*)decrypted);
        return true;
    }
    return false;
}
