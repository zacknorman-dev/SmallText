#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <Arduino.h>
#include <Wire.h>

// CardKB special key codes
#define CARDKB_UP    0xB5
#define CARDKB_DOWN  0xB6
#define CARDKB_LEFT  0xB4
#define CARDKB_RIGHT 0xB7
#define CARDKB_ENTER 0xB2
#define CARDKB_BS    0xB3
#define CARDKB_TAB   0x09  // Tab key for power management

// Common I2C keyboard addresses
#define KEYBOARD_I2C_ADDR 0x5F  // M5Stack CardKB I2C address

// Keyboard buffer size
#define KEY_BUFFER_SIZE 64

class Keyboard {
private:
    TwoWire* wire;
    uint8_t i2cAddress;
    bool keyboardPresent;  // Track if hardware keyboard is connected
    
    String inputBuffer;
    char lastKey;
    char currentKey;  // Current key being held
    unsigned long lastKeyTime;
    
    // Hardware buffer for capturing fast keypresses
    char keyBuffer[KEY_BUFFER_SIZE];
    volatile uint8_t bufferHead;
    volatile uint8_t bufferTail;
    
    // Track for stuck key detection
    char lastRawKey;  // Track last raw I2C read to detect stuck keys
    uint8_t sameKeyCount;  // Count repeated identical reads
    
    char readKey();
    void bufferKey(char key);
    char getBufferedKey();
    bool hasBufferedKeys();
    
public:
    Keyboard(TwoWire* w = &Wire, uint8_t addr = KEYBOARD_I2C_ADDR);
    
    bool begin();
    void update();  // Call this in loop()
    bool isKeyboardPresent();  // Check if hardware keyboard detected
    
    // Input handling
    bool hasInput();
    String getInput();
    void clearInput();
    
    // Get current input buffer (for display)
    String getCurrentBuffer() { return inputBuffer; }
    
    // Check for specific keys
    bool isEnterPressed();
    bool isBackspacePressed();
    bool isUpPressed();
    bool isDownPressed();
    bool isLeftPressed();
    bool isRightPressed();
    bool isEscPressed();
    
    // Check key without consuming (for hold detection)
    bool isTabHeld();
    
    // Clear special key state (call after handling)
    void clearSpecialKey() { currentKey = 0; }
};

#endif
