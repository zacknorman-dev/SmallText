#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <Arduino.h>
#include <ChaCha.h>
#include <Poly1305.h>

#define NONCE_SIZE 12
#define TAG_SIZE 16
#define MAX_PLAINTEXT 200
#define MAX_CIPHERTEXT (MAX_PLAINTEXT + NONCE_SIZE + TAG_SIZE)

class Encryption {
private:
    ChaCha chacha;
    Poly1305 poly1305;
    uint8_t key[32];
    
    void generateNonce(uint8_t* nonce);
    
public:
    Encryption();
    
    void setKey(const uint8_t* newKey);
    const uint8_t* getKey() const { return key; }  // Get current encryption key
    
    // Returns encrypted message length, or 0 on error
    // Output format: [nonce(12)][ciphertext][tag(16)]
    int encrypt(const uint8_t* plaintext, size_t plaintextLen, 
                uint8_t* output, size_t outputMaxLen);
    
    // Returns decrypted message length, or -1 on error/authentication failure
    int decrypt(const uint8_t* input, size_t inputLen,
                uint8_t* output, size_t outputMaxLen);
    
    // Helper for string encryption/decryption
    bool encryptString(const String& plaintext, uint8_t* output, size_t outputMaxLen, size_t* outputLen);
    bool decryptString(const uint8_t* input, size_t inputLen, String& plaintext);
};

#endif
