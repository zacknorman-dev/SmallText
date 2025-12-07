#include "Keyboard.h"

#define KEY_DEBOUNCE_MS 30  // Minimal debounce - just catch electrical bounce

Keyboard::Keyboard(TwoWire* w, uint8_t addr) {
    wire = w;
    i2cAddress = addr;
    inputBuffer = "";
    lastKey = 0;
    currentKey = 0;
    lastKeyTime = 0;
    keyboardPresent = false;  // Will check on begin()
    bufferHead = 0;
    bufferTail = 0;
    lastRawKey = 0;
    sameKeyCount = 0;
    memset(keyBuffer, 0, KEY_BUFFER_SIZE);
}

bool Keyboard::begin() {
    // Don't call wire->begin() here - it should be initialized with pins in main
    
    // Clear all state to prevent phantom key presses from garbage data
    inputBuffer = "";
    lastKey = 0;
    currentKey = 0;
    lastKeyTime = 0;
    bufferHead = 0;
    bufferTail = 0;
    lastRawKey = 0;
    sameKeyCount = 0;
    memset(keyBuffer, 0, KEY_BUFFER_SIZE);
    Serial.println("[Keyboard] State cleared: currentKey=0, inputBuffer empty");
    
    // Try to detect keyboard with retry logic for reliability
    const int MAX_RETRIES = 5;
    const int RETRY_DELAY_MS = 50;
    
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        wire->beginTransmission(i2cAddress);
        int error = wire->endTransmission();
        
        if (error == 0) {
            Serial.print(F("[Keyboard] CardKB found at 0x"));
            Serial.print(i2cAddress, HEX);
            if (attempt > 1) {
                Serial.print(F(" (attempt "));
                Serial.print(attempt);
                Serial.print(F(")"));
            }
            Serial.println();
            keyboardPresent = true;
            return true;
        }
        
        // Failed, wait before retry
        if (attempt < MAX_RETRIES) {
            Serial.print(F("[Keyboard] Detection attempt "));
            Serial.print(attempt);
            Serial.print(F(" failed (error "));
            Serial.print(error);
            Serial.println(F("), retrying..."));
            delay(RETRY_DELAY_MS);
        }
    }
    
    // All attempts failed
    Serial.print(F("[Keyboard] No keyboard at 0x"));
    Serial.print(i2cAddress, HEX);
    Serial.println(F(" after 5 attempts - using Serial input"));
    keyboardPresent = false;
    return false;  // Will use Serial as fallback
}

char Keyboard::readKey() {
    static uint32_t readCounter = 0;
    
    // Only try I2C if keyboard is present
    if (!keyboardPresent) {
        // Fallback to Serial for testing
        if (Serial.available()) {
            return Serial.read();
        }
        return 0;
    }
    
    if (++readCounter % 10000 == 0) {  // Log every 10000 reads
        Serial.print("[KB-READ] readKey() called ");
        Serial.print(readCounter);
        Serial.println(" times");
    }
    
    // This is a generic implementation - you'll need to adjust for your specific keyboard
    // Most I2C keyboards send ASCII characters when keys are pressed
    
    wire->requestFrom(i2cAddress, (uint8_t)1);
    
    if (wire->available()) {
        char key = wire->read();
        
        // Detect stuck keys - if same non-zero value repeats 50+ times consecutively, ignore it
        // This allows legitimate rapid key presses but blocks electrically stuck keys
        if (key != 0 && key == lastRawKey) {
            sameKeyCount++;
            if (sameKeyCount >= 50) {
                // Stuck key detected - only log occasionally to avoid spam
                if (sameKeyCount == 50) {
                    Serial.print("[KB-READ] STUCK KEY detected: 0x");
                    Serial.print((uint8_t)key, HEX);
                    Serial.println(" - ignoring repeated reads");
                }
                return 0;  // Ignore stuck key
            }
        } else {
            lastRawKey = key;
            sameKeyCount = 1;
        }
        
        // Debug: Log byte value read from I2C
        if (key != 0 && key != 0xFF) {
            Serial.print("[KB-READ] I2C byte: 0x");
            Serial.print((uint8_t)key, HEX);
            Serial.print(" (");
            Serial.print((uint8_t)key, DEC);
            Serial.println(")");
        }
        
        // Validate key is in expected ranges
        // CardKB special keys: 0xB2-0xB7, printable ASCII: 0x20-0x7E, CR/LF: 0x0A/0x0D, BS/DEL: 0x08/0x7F
        if (key != 0 && key != 0xFF) {
            uint8_t ukey = (uint8_t)key;
            if ((ukey >= 0x20 && ukey <= 0x7E) ||  // Printable ASCII
                (ukey >= 0xB2 && ukey <= 0xB7) ||  // CardKB special keys
                ukey == 0x0A || ukey == 0x0D ||    // Line feed / Carriage return (ENTER)
                ukey == 0x08 || ukey == 0x7F) {    // Backspace / Delete
                return key;
            } else {
                Serial.print("[KB-READ] Invalid key code: 0x");
                Serial.print(ukey, HEX);
                Serial.println(" - ignoring");
                return 0;
            }
        }
    }
    
    // Fallback to Serial for testing
    if (Serial.available()) {
        return Serial.read();
    }
    
    return 0;
}

bool Keyboard::isKeyboardPresent() {
    return keyboardPresent;
}

void Keyboard::bufferKey(char key) {
    uint8_t nextHead = (bufferHead + 1) % KEY_BUFFER_SIZE;
    if (nextHead != bufferTail) {  // Buffer not full
        keyBuffer[bufferHead] = key;
        bufferHead = nextHead;
    }
}

char Keyboard::getBufferedKey() {
    if (bufferHead == bufferTail) {
        return 0;  // Buffer empty
    }
    char key = keyBuffer[bufferTail];
    bufferTail = (bufferTail + 1) % KEY_BUFFER_SIZE;
    return key;
}

bool Keyboard::hasBufferedKeys() {
    return bufferHead != bufferTail;
}

void Keyboard::update() {
    // First, read any new keys from hardware and buffer them
    char key = readKey();
    if (key != 0) {
        bufferKey(key);
    }
    
    // Clear debounce after timeout to allow same key to be pressed again
    unsigned long currentTime = millis();
    if (lastKey != 0 && (currentTime - lastKeyTime) >= KEY_DEBOUNCE_MS) {
        lastKey = 0;
    }
    
    // Now process buffered keys with debouncing
    if (!hasBufferedKeys()) {
        return;
    }
    
    key = getBufferedKey();
    
    if (key != 0) {
        // Debounce: only reject if SAME key within debounce window
        if (key == lastKey && (currentTime - lastKeyTime) < KEY_DEBOUNCE_MS) {
            return;
        }
        
        lastKey = key;
        lastKeyTime = currentTime;
        
        // Store current key for special key checks
        currentKey = key;
        
        // Handle special keys that don't go into buffer
        if (key == '\n' || key == '\r' || key == 0xB2 || key == 0x0D) {
            // Enter key (0xB2 is CardKB enter, 0x0D is carriage return)
            Serial.println("[Keyboard] ENTER pressed");
            // Don't return - keep currentKey set so isEnterPressed() can detect it
        } else if (key == 0xB4) {
            // Left arrow (0xB4 is CardKB left)
            Serial.println("[Keyboard] LEFT pressed");
            // Don't return - keep currentKey set so isLeftPressed() can detect it
        } else if (key == 0xB7) {
            // Right arrow (0xB7 is CardKB right)
            Serial.println("[Keyboard] RIGHT pressed");
            // Don't return - keep currentKey set so isRightPressed() can detect it
        } else if (key == 0xB5) {
            // Up arrow (0xB5 is CardKB up)
            Serial.println("[Keyboard] UP pressed");
            // Don't return - keep currentKey set so isUpPressed() can detect it
        } else if (key == 0xB6) {
            // Down arrow (0xB6 is CardKB down)
            Serial.println("[Keyboard] DOWN pressed");
            // Don't return - keep currentKey set so isDownPressed() can detect it
        } else if (key == 8 || key == 127 || key == 0xB3) {
            // Backspace or Delete (0xB3 is CardKB backspace)
            Serial.printf("[Keyboard] BACKSPACE pressed! Raw key value: %d (0x%02X)\n", key, key);
            Serial.printf("[Keyboard] Setting currentKey to: %d (0x%02X)\n", key, key);
            // Don't return - keep currentKey set so isBackspacePressed() can detect it
        } else if (key >= 32 && key <= 126) {
            // Printable ASCII character - add to buffer
            inputBuffer += key;
            currentKey = 0;  // Clear immediately - printable chars handled via inputBuffer
        } else {
            // Unrecognized key - log it
            Serial.printf("[Keyboard] Unrecognized key: %d (0x%02X)\n", key, key);
            currentKey = 0;  // Clear unrecognized keys too
        }
    }
    // Special keys (enter, arrows, backspace) remain in currentKey until explicitly consumed
}

bool Keyboard::hasInput() {
    return inputBuffer.length() > 0;
}

String Keyboard::getInput() {
    // Don't clear buffer - let caller decide when to clear
    // This prevents losing keys during display refreshes
    return inputBuffer;
}

void Keyboard::clearInput() {
    inputBuffer = "";
}

bool Keyboard::isEnterPressed() {
    bool pressed = (currentKey == '\n' || currentKey == '\r' || currentKey == CARDKB_ENTER);
    if (pressed) {
        currentKey = 0;  // Consume the key
    }
    return pressed;
}

bool Keyboard::isBackspacePressed() {
    bool pressed = (currentKey == 8 || currentKey == 127 || currentKey == CARDKB_BS);
    if (pressed) {
        currentKey = 0;  // Consume the key
    }
    return pressed;
}

bool Keyboard::isUpPressed() {
    bool pressed = (currentKey == CARDKB_UP);
    if (pressed) {
        currentKey = 0;  // Consume the key
    }
    return pressed;
}

bool Keyboard::isDownPressed() {
    bool pressed = (currentKey == CARDKB_DOWN);
    if (pressed) {
        currentKey = 0;  // Consume the key
    }
    return pressed;
}

bool Keyboard::isLeftPressed() {
    bool pressed = (currentKey == CARDKB_LEFT);
    if (pressed) {
        currentKey = 0;  // Consume the key
    }
    return pressed;
}

bool Keyboard::isRightPressed() {
    bool pressed = (currentKey == CARDKB_RIGHT);
    if (pressed) {
        currentKey = 0;  // Consume the key
    }
    return pressed;
}
