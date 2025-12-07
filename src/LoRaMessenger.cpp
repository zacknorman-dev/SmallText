#include "LoRaMessenger.h"
#include "Logger.h"

// Static member for interrupt-driven reception
volatile bool LoRaMessenger::receivedFlag = false;

void LoRaMessenger::setFlag(void) {
    receivedFlag = true;
}

LoRaMessenger::LoRaMessenger() {
    radio = nullptr;
    encryption = nullptr;
    myVillageId = "";
    myVillageName = "";
    myUsername = "";
    myMAC = ESP.getEfuseMac();
    onMessageReceived = nullptr;
    onMessageAcked = nullptr;
    onMessageRead = nullptr;
    onVillageNameReceived = nullptr;
    delayCallback = nullptr;
    lastSeenCleanup = 0;
    lastTransmissionCleanup = 0;
}

uint32_t LoRaMessenger::generateVillageId(const String& villageName) {
    // Simple hash function to generate village ID from name
    uint32_t hash = 0;
    for (size_t i = 0; i < villageName.length(); i++) {
        hash = hash * 31 + villageName[i];
    }
    return hash;
}

uint32_t LoRaMessenger::hashPacket(const uint8_t* data, size_t len) {
    // Compute simple hash of packet for echo detection
    // Use first 16 bytes for speed (nonce + start of ciphertext)
    uint32_t hash = 0;
    size_t hashLen = (len < 16) ? len : 16;
    for (size_t i = 0; i < hashLen; i++) {
        hash = ((hash << 5) + hash) + data[i];  // hash * 33 + byte
    }
    return hash;
}

bool LoRaMessenger::begin(int csPin, int dio1Pin, int resetPin, int busyPin) {
    // Initialize SX1262 for Heltec Vision Master E290
    radio = new SX1262(new Module(csPin, dio1Pin, resetPin, busyPin));
    
    Serial.print(F("[LoRa] Initializing ... "));
    
    // For E290, MAXIMUM RANGE settings:
    // Frequency: 915 MHz (US)
    // Bandwidth: 125 kHz
    // Spreading Factor: 12 (maximum range)
    // Coding Rate: 4/7
    // Output Power: 22 dBm
    
    int state = radio->begin(915.0, 125.0, 12, 7, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 22);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("success!"));
        
        // Set up interrupt-driven async reception
        radio->setPacketReceivedAction(setFlag);
        receivedFlag = false;
        
        // Start receiver in async mode
        radio->startReceive();
        
        return true;
    } else {
        Serial.print(F("failed, code "));
        Serial.println(state);
        return false;
    }
}

void LoRaMessenger::setEncryption(Encryption* enc) {
    encryption = enc;
}

void LoRaMessenger::setVillageInfo(const String& villageId, const String& villageName, const String& username) {
    myVillageId = villageId;
    myVillageName = villageName;
    myUsername = username;
    Serial.println("[LoRaMessenger] Village Info Set:");
    Serial.println("  ID: " + myVillageId);
    Serial.println("  Name: " + myVillageName);
    Serial.println("  User: " + myUsername);
}

void LoRaMessenger::setMessageCallback(void (*callback)(const Message& msg)) {
    onMessageReceived = callback;
}

void LoRaMessenger::setAckCallback(void (*callback)(const String& messageId, const String& fromMAC)) {
    onMessageAcked = callback;
}

void LoRaMessenger::setReadCallback(void (*callback)(const String& messageId, const String& fromMAC)) {
    onMessageRead = callback;
}

void LoRaMessenger::setVillageNameCallback(void (*callback)(const String& villageName)) {
    onVillageNameReceived = callback;
}

void LoRaMessenger::setDelayCallback(void (*callback)(unsigned long ms)) {
    delayCallback = callback;
}

bool LoRaMessenger::sendMessage(const String& message) {
    // Legacy method - calls sendShout for backward compatibility
    return sendShout(message);
}

void LoRaMessenger::checkForMessages() {
    // Legacy method - calls loop for backward compatibility
    loop();
}

void LoRaMessenger::setFrequency(float freq) {
    if (radio) {
        radio->setFrequency(freq);
    }
}

void LoRaMessenger::setBandwidth(float bw) {
    if (radio) {
        radio->setBandwidth(bw);
    }
}

void LoRaMessenger::setSpreadingFactor(uint8_t sf) {
    if (radio) {
        radio->setSpreadingFactor(sf);
    }
}

void LoRaMessenger::setOutputPower(int8_t power) {
    if (radio) {
        radio->setOutputPower(power);
    }
}

bool LoRaMessenger::sendRaw(const String& message) {
    if (!radio) return false;
    
    int state = radio->transmit(message.c_str());
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("[LoRa] Raw message sent"));
        radio->startReceive();  // Go back to receive mode
        return true;
    } else {
        Serial.print(F("[LoRa] Send failed, code "));
        Serial.println(state);
        return false;
    }
}

String LoRaMessenger::checkForMessage() {
    if (!radio) return "";
    
    // Only check if interrupt flag is set (new packet arrived)
    if (!receivedFlag) {
        return "";  // No new packet
    }
    
    // Clear the flag FIRST to prevent re-processing
    receivedFlag = false;
    
    // Read the packet
    uint8_t buffer[256];
    int state = radio->readData(buffer, sizeof(buffer));
    
    // Restart receive mode for next packet
    radio->startReceive();
    
    // Only process if we successfully read a packet
    if (state == RADIOLIB_ERR_NONE) {
        size_t len = radio->getPacketLength();
        
        // Ignore empty or oversized packets
        if (len == 0 || len > sizeof(buffer) - 1) {
            return "";
        }
        
        // Null terminate and convert to String
        buffer[len] = 0;
        String received = String((char*)buffer);
        
        Serial.print(F("[LoRa] Received: "));
        Serial.println(received);
        Serial.print(F("[LoRa] RSSI: "));
        Serial.print(radio->getRSSI());
        Serial.println(F(" dBm"));
        
        return received;
    }
    
    // Read failed
    return "";
}


// ============================================================================
// NEW MESSAGE PROTOCOL IMPLEMENTATION
// ============================================================================

String LoRaMessenger::generateMessageId() {
    // Generate unique message ID: timestamp + random
    static uint32_t counter = 0;
    counter++;
    char id[16];
    snprintf(id, sizeof(id), "%08lx%04x", millis(), (counter & 0xFFFF));
    return String(id);
}

String LoRaMessenger::formatMessage(MessageType type, const String& target, const String& content, int maxHop) {
    // Format: "TYPE:villageId:target:senderUsername:senderMAC:msgId:content:hop:maxHop"
    String typeStr;
    switch (type) {
        case MSG_SHOUT: typeStr = "SHOUT"; break;
        case MSG_GROUP: typeStr = "GROUP"; break;
        case MSG_WHISPER: typeStr = "WHISPER"; break;
        case MSG_ACK: typeStr = "ACK"; break;
        default: typeStr = "UNKNOWN"; break;
    }
    
    lastSentMessageId = generateMessageId();  // Store the generated ID
    
    // Normalize MAC to lowercase for consistent comparison
    String macStr = String(myMAC, HEX);
    macStr.toLowerCase();
    
    String msg = typeStr + ":" + myVillageId + ":" + target + ":" + 
                 myUsername + ":" + macStr + ":" + lastSentMessageId + ":" + 
                 content + ":0:" + String(maxHop);
    
    return msg;
}

ParsedMessage LoRaMessenger::parseMessage(const String& decrypted) {
    ParsedMessage pm;
    
    // Split by colons: TYPE:village:target:senderName:senderMAC:msgId:content:hop:maxHop
    int idx = 0;
    int lastIdx = 0;
    String parts[9];
    int partCount = 0;
    
    while (idx < decrypted.length() && partCount < 9) {
        idx = decrypted.indexOf(':', lastIdx);
        if (idx == -1) {
            parts[partCount++] = decrypted.substring(lastIdx);
            break;
        }
        parts[partCount++] = decrypted.substring(lastIdx, idx);
        lastIdx = idx + 1;
    }
    
    if (partCount < 9) {
        Serial.print(F("[LoRa] Invalid message format - got "));
        Serial.print(partCount);
        Serial.print(F(" parts: "));
        Serial.println(decrypted);
        return pm;  // Returns MSG_UNKNOWN
    }
    
    // Parse type
    if (parts[0] == "SHOUT") pm.type = MSG_SHOUT;
    else if (parts[0] == "GROUP") pm.type = MSG_GROUP;
    else if (parts[0] == "WHISPER") pm.type = MSG_WHISPER;
    else if (parts[0] == "ACK") pm.type = MSG_ACK;
    else if (parts[0] == "READ_RECEIPT") pm.type = MSG_READ_RECEIPT;
    else if (parts[0] == "VILLAGE_NAME_REQUEST") pm.type = MSG_VILLAGE_NAME_REQUEST;
    else if (parts[0] == "VILLAGE_ANNOUNCE") pm.type = MSG_VILLAGE_ANNOUNCE;
    else pm.type = MSG_UNKNOWN;
    
    pm.villageId = parts[1];      // UUID for filtering
    pm.villageName = "";           // Not transmitted anymore
    pm.target = parts[2];
    pm.senderName = parts[3];
    pm.senderMAC = parts[4];
    pm.senderMAC.toLowerCase();  // Normalize for comparison
    pm.messageId = parts[5];
    pm.content = parts[6];
    pm.currentHop = parts[7].toInt();
    pm.maxHop = parts[8].toInt();
    
    return pm;
}

bool LoRaMessenger::isGarbage(const String& text) {
    // Detect failed decryption: >30% unprintable characters
    int unprintableCount = 0;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c < 32 && c != '\n' && c != '\t' && c != '\r') {
            unprintableCount++;
        }
    }
    
    float unprintableRatio = (float)unprintableCount / text.length();
    return unprintableRatio > 0.3;
}

bool LoRaMessenger::sendShout(const String& message) {
    if (!encryption) {
        logger.critical("SHOUT send failed: no encryption pointer set");
        Serial.println(F("[LoRa] No encryption set"));
        return false;
    }
    
    // SHOUT messages use mesh immediately (max_hop=3)
    String formatted = formatMessage(MSG_SHOUT, "*", message, 3);
    
    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        logger.error("SHOUT encryption failed");
        Serial.println(F("[LoRa] Encryption failed"));
        return false;
    }
    
    // Transmit
    uint32_t packetHash = hashPacket(encrypted, encryptedLen);
    int state = radio->transmit(encrypted, encryptedLen);
    radio->startReceive();  // Return to receive mode
    
    // Track transmission to detect echo
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
        logger.info("SHOUT sent: " + message.substring(0, 20) + (message.length() > 20 ? "..." : ""));
    } else {
        logger.error("SHOUT transmit failed, code=" + String(state));
    }
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.print(F("[LoRa] SHOUT sent: "));
        Serial.println(message);
        return true;
    }
    
    Serial.print(F("[LoRa] Transmit failed, code "));
    Serial.println(state);
    return false;
}

bool LoRaMessenger::sendGroup(const String& groupName, const String& message) {
    if (!encryption) {
        logger.error("GROUP send failed: no encryption");
        Serial.println(F("[LoRa] No encryption set"));
        return false;
    }
    
    // GROUP messages try direct first (max_hop=0), will escalate if needed
    String formatted = formatMessage(MSG_GROUP, groupName, message, 0);
    
    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        logger.error("GROUP encryption failed");
        Serial.println(F("[LoRa] Encryption failed"));
        return false;
    }
    
    // Transmit
    uint32_t packetHash = hashPacket(encrypted, encryptedLen);
    int state = radio->transmit(encrypted, encryptedLen);
    radio->startReceive();  // Return to receive mode
    
    // Track transmission to detect echo
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
        logger.info("GROUP sent to=" + groupName + " msg=" + message.substring(0, 20));
    } else {
        logger.error("GROUP transmit failed, code=" + String(state));
    }
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
    }
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.print(F("[LoRa] GROUP sent to "));
        Serial.print(groupName);
        Serial.print(F(": "));
        Serial.println(message);
        return true;
    }
    
    Serial.print(F("[LoRa] Transmit failed, code "));
    Serial.println(state);
    return false;
}

bool LoRaMessenger::sendWhisper(const String& recipientMAC, const String& message) {
    if (!encryption) {
        Serial.println(F("[LoRa] No encryption set"));
        return false;
    }
    
    // WHISPER messages try direct first (max_hop=0), will escalate if needed
    String formatted = formatMessage(MSG_WHISPER, recipientMAC, message, 0);
    
    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        Serial.println(F("[LoRa] Encryption failed"));
        return false;
    }
    
    // Transmit
    uint32_t packetHash = hashPacket(encrypted, encryptedLen);
    int state = radio->transmit(encrypted, encryptedLen);
    radio->startReceive();  // Return to receive mode
    
    // Track transmission to detect echo
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
    }
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.print(F("[LoRa] WHISPER sent to "));
        Serial.print(recipientMAC);
        Serial.print(F(": "));
        Serial.println(message);
        return true;
    }
    
    Serial.print(F("[LoRa] Transmit failed, code "));
    Serial.println(state);
    return false;
}

bool LoRaMessenger::sendAck(const String& messageId, const String& targetMAC) {
    if (!encryption) return false;
    
    // Generate unique ACK message ID (different from original message)
    String ackId = generateMessageId();
    
    // Normalize MAC to lowercase for consistency
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: ACK:villageId:targetMAC:sender:senderMAC:ackId:originalMessageId:0:1
    String formatted = String("ACK:") + myVillageId + ":" + targetMAC + ":" +
                      myUsername + ":" + myMacStr + ":" + ackId + ":" + messageId + ":0:1";
    
    Serial.println("[LoRa] ACK plaintext: " + formatted);
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        Serial.println("[LoRa] ACK encryption FAILED!");
        return false;
    }
    
    Serial.print("[LoRa] ACK encrypted length: ");
    Serial.println(encryptedLen);
    Serial.print("[LoRa] ACK encrypted first 16 bytes: ");
    for (int i = 0; i < min(16, (int)encryptedLen); i++) {
        Serial.print(encrypted[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    
    delay(50);  // Small delay to let radio settle from previous RX before TX
    uint32_t packetHash = hashPacket(encrypted, encryptedLen);
    int state = radio->transmit(encrypted, encryptedLen);
    
    // Track transmission to detect echo
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
    }
    
    // Use callback for responsive delay if available, otherwise regular delay
    if (delayCallback) {
        delayCallback(3000);
    } else {
        delay(3000);
    }
    radio->clearDio1Action();
    radio->startReceive();
    radio->setDio1Action(setFlag);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[LoRa] ACK sent for message: " + messageId);
        return true;
    }
    return false;
}

bool LoRaMessenger::sendReadReceipt(const String& messageId, const String& targetMAC) {
    if (!encryption) return false;
    
    // Generate unique read receipt ID (different from original message)
    String receiptId = generateMessageId();
    
    // Normalize MAC to lowercase for consistency
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: READ_RECEIPT:villageId:targetMAC:sender:senderMAC:receiptId:originalMessageId:0:1
    String formatted = String("READ_RECEIPT:") + myVillageId + ":" + targetMAC + ":" +
                      myUsername + ":" + myMacStr + ":" + receiptId + ":" + messageId + ":0:1";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        return false;
    }
    
    delay(50);  // Small delay to let radio settle from previous RX before TX
    uint32_t packetHash = hashPacket(encrypted, encryptedLen);
    int state = radio->transmit(encrypted, encryptedLen);
    
    // Track transmission to detect echo
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
    }
    
    // Use callback for responsive delay if available, otherwise regular delay
    if (delayCallback) {
        delayCallback(3000);
    } else {
        delay(3000);
    }
    radio->clearDio1Action();
    radio->startReceive();
    radio->setDio1Action(setFlag);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[LoRa] Read receipt sent for message: " + messageId);
        return true;
    }
    return false;
}

bool LoRaMessenger::sendVillageNameAnnouncement() {
    if (!encryption) return false;
    
    // Generate unique message ID
    String msgId = generateMessageId();
    
    // Normalize MAC to lowercase for consistency
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: VILLAGE_ANNOUNCE:villageId:*:sender:senderMAC:msgId:villageName:0:1
    // Target is "*" for broadcast, content is the village name
    String formatted = String("VILLAGE_ANNOUNCE:") + myVillageId + ":*:" +
                      myUsername + ":" + myMacStr + ":" + msgId + ":" + myVillageName + ":0:1";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        return false;
    }
    
    delay(50);  // Small delay to let radio settle
    uint32_t packetHash = hashPacket(encrypted, encryptedLen);
    int state = radio->transmit(encrypted, encryptedLen);
    
    // Track transmission to detect echo
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
    }
    
    // Use callback for responsive delay if available
    if (delayCallback) {
        delayCallback(3000);
    } else {
        delay(3000);
    }
    radio->clearDio1Action();
    radio->startReceive();
    radio->setDio1Action(setFlag);
    
    if (state == RADIOLIB_ERR_NONE) {
        logger.info("VILLAGE_ANNOUNCE sent: " + myVillageName);
        Serial.println("[LoRa] Village name announcement sent: " + myVillageName);
        return true;
    }
    
    logger.error("VILLAGE_ANNOUNCE transmit failed, code=" + String(state));
    Serial.print(F("[LoRa] Village announcement failed, code "));
    Serial.println(state);
    return false;
}

bool LoRaMessenger::sendVillageNameRequest() {
    if (!encryption) return false;
    
    // Generate unique message ID
    String msgId = generateMessageId();
    
    // Normalize MAC to lowercase for consistency
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: VILLAGE_NAME_REQUEST:villageId:*:sender:senderMAC:msgId:request:0:1
    // Target is "*" for broadcast, content is "request"
    String formatted = String("VILLAGE_NAME_REQUEST:") + myVillageId + ":*:" +
                      myUsername + ":" + myMacStr + ":" + msgId + ":request:0:1";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        return false;
    }
    
    delay(50);  // Small delay to let radio settle
    uint32_t packetHash = hashPacket(encrypted, encryptedLen);
    int state = radio->transmit(encrypted, encryptedLen);
    
    // Track transmission to detect echo
    if (state == RADIOLIB_ERR_NONE) {
        recentTransmissions[packetHash] = millis();
    }
    
    // Use callback for responsive delay if available
    if (delayCallback) {
        delayCallback(3000);
    } else {
        delay(3000);
    }
    radio->clearDio1Action();
    radio->startReceive();
    radio->setDio1Action(setFlag);
    
    if (state == RADIOLIB_ERR_NONE) {
        logger.info("VILLAGE_NAME_REQUEST sent");
        Serial.println("[LoRa] Village name request sent");
        return true;
    }
    
    logger.error("VILLAGE_NAME_REQUEST transmit failed, code=" + String(state));
    Serial.print(F("[LoRa] Village name request failed, code "));
    Serial.println(state);
    return false;
}

void LoRaMessenger::loop() {
    // Clean up old seen message IDs every 60 seconds
    if (millis() - lastSeenCleanup > 60000) {
        seenMessageIds.clear();
        lastSeenCleanup = millis();
    }
    
    // Clean up old transmission hashes every 10 seconds
    if (millis() - lastTransmissionCleanup > 10000) {
        unsigned long cutoff = millis() - 5000;  // Keep last 5 seconds
        for (auto it = recentTransmissions.begin(); it != recentTransmissions.end(); ) {
            if (it->second < cutoff) {
                it = recentTransmissions.erase(it);
            } else {
                ++it;
            }
        }
        lastTransmissionCleanup = millis();
    }
    
    // Check for new messages
    if (!receivedFlag) {
        return;
    }
    
    receivedFlag = false;
    
    if (!radio || !encryption) {
        radio->startReceive();
        return;
    }
    
    // Read packet
    uint8_t buffer[256];
    int state = radio->readData(buffer, sizeof(buffer));
    
    // Restart receiver
    radio->startReceive();
    
    if (state != RADIOLIB_ERR_NONE) {
        return;
    }
    
    size_t len = radio->getPacketLength();
    if (len == 0 || len > sizeof(buffer) - 1) {
        return;
    }
    
    // Check for echo - did we recently transmit this exact packet?
    uint32_t packetHash = hashPacket(buffer, len);
    if (recentTransmissions.count(packetHash)) {
        Serial.println(F("[LoRa] Own transmission echo detected, dropped"));
        return;
    }
    
    Serial.print(F("[LoRa] Packet size: "));
    Serial.println(len);
    
    // Debug: Show encryption state
    Serial.print(F("[LoRa] Decrypting with key: "));
    if (encryption) {
        Serial.println(F("encryption object available"));
    } else {
        Serial.println(F("NO ENCRYPTION OBJECT!"));
    }
    
    // Decrypt
    uint8_t decrypted[256];
    int decryptedLen = encryption->decrypt(buffer, len, decrypted, sizeof(decrypted));
    
    if (decryptedLen <= 0) {
        logger.error("Message decryption failed, len=" + String(len));
        Serial.println(F("[LoRa] Decryption failed"));
        Serial.print(F("[LoRa] First 16 bytes: "));
        for (size_t i = 0; i < min((size_t)16, len); i++) {
            Serial.print(buffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();
        return;
    }
    
    decrypted[decryptedLen] = '\0';
    String decryptedStr = String((char*)decrypted);
    
    // Check for garbage (failed decryption from wrong key)
    if (isGarbage(decryptedStr)) {
        logger.error("Garbage message detected (wrong key?)");
        Serial.println(F("[LoRa] Garbage message dropped"));
        Serial.print("[LoRa] Decrypted content: ");
        Serial.println(decryptedStr);
        return;
    }
    
    // Debug: print decrypted content for analysis
    Serial.print("[LoRa] Decrypted: ");
    Serial.println(decryptedStr);
    
    // Parse message
    ParsedMessage pm = parseMessage(decryptedStr);
    if (pm.type == MSG_UNKNOWN) {
        logger.error("Unknown message type");
        return;
    }
    
    // Ignore messages from ourselves (echo from our own transmission)
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    if (pm.senderMAC == myMacStr) {
        Serial.println(F("[LoRa] Own message echo, dropped"));
        return;
    }
    
    // Check if we've seen this message before (duplicate detection)
    if (seenMessageIds.count(pm.messageId)) {
        Serial.println(F("[LoRa] Duplicate message dropped"));
        return;
    }
    seenMessageIds.insert(pm.messageId);
    
    // Check if message is for our village (by UUID)
    Serial.println("[LoRa] UUID Check:");
    Serial.println("  My UUID: " + myVillageId);
    Serial.println("  Msg UUID: " + pm.villageId);
    Serial.println("  Match: " + String(pm.villageId == myVillageId ? "YES" : "NO"));
    
    if (pm.villageId != myVillageId) {
        logger.info("Message for wrong village UUID, dropped");
        Serial.println(F("[LoRa] Wrong village UUID, dropped"));
        return;
    }
    
    logger.info("MSG RX: type=" + String(pm.type) + " from=" + pm.senderMAC + " id=" + pm.messageId);
    Serial.print(F("[LoRa] Received "));
    Serial.print(pm.type == MSG_SHOUT ? "SHOUT" : pm.type == MSG_GROUP ? "GROUP" : "WHISPER");
    Serial.print(F(" from "));
    Serial.print(pm.senderMAC);
    Serial.print(F(": "));
    Serial.println(pm.content);
    Serial.print(F("[LoRa] RSSI: "));
    Serial.print(radio->getRSSI());
    Serial.println(F(" dBm"));
    
    // Handle the message (forward if needed, display if for us)
    handleReceivedMessage(pm);
}

void LoRaMessenger::handleReceivedMessage(const ParsedMessage& msg) {
    // Normalize our MAC for comparison
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Handle ACK messages
    if (msg.type == MSG_ACK && msg.target == myMacStr) {
        // Original message ID is in the content field (position 6)
        Serial.println("[LoRa] Received ACK for message: " + msg.content);
        if (onMessageAcked) {
            onMessageAcked(msg.content, msg.senderMAC);
        }
        return;  // Don't process ACKs further
    }
    
    // Handle read receipts
    if (msg.type == MSG_READ_RECEIPT && msg.target == myMacStr) {
        // Original message ID is in the content field (position 6)
        Serial.println("[LoRa] Received read receipt for message: " + msg.content);
        if (onMessageRead) {
            onMessageRead(msg.content, msg.senderMAC);
        }
        return;  // Don't process read receipts further
    }
    
    // Handle village name announcements
    if (msg.type == MSG_VILLAGE_ANNOUNCE) {
        Serial.println("[LoRa] Received village name announcement: " + msg.content);
        if (onVillageNameReceived) {
            onVillageNameReceived(msg.content);
        }
        return;  // Don't process announcements as regular messages
    }
    
    // Handle village name requests (owner responds with announcement)
    if (msg.type == MSG_VILLAGE_NAME_REQUEST) {
        Serial.println("[LoRa] Received village name request from " + msg.senderMAC);
        // Immediately send village name announcement back to requester
        Serial.println("[LoRa] Sending village name announcement: " + myVillageName);
        sendVillageNameAnnouncement();
        return;  // Don't process requests as regular messages
    }
    
    // Handle read receipts
    if (msg.type == MSG_READ_RECEIPT && msg.target == String(myMAC, HEX)) {
        // Original message ID is in the content field (position 6)
        Serial.println("[LoRa] Received read receipt for message: " + msg.content);
        if (onMessageRead) {
            onMessageRead(msg.content, msg.senderMAC);
        }
        return;  // Don't process read receipts further
    }
    
    // Check if message is for me
    bool forMe = false;
    if (msg.type == MSG_SHOUT) {
        forMe = true;  // SHOUT is for everyone
    } else if (msg.type == MSG_GROUP) {
        forMe = true;  // GROUP filtering happens in UI
    } else if (msg.type == MSG_WHISPER) {
        String myMacStr = String(myMAC, HEX);
        myMacStr.toLowerCase();
        forMe = (msg.target == myMacStr);  // WHISPER only if target matches
    }
    
    if (forMe) {
        // Send ACK back to sender
        Serial.println("[LoRa] Sending ACK for message: " + msg.messageId + " to " + msg.senderMAC);
        sendAck(msg.messageId, msg.senderMAC);
        
        // Display message (call callback if set)
        if (onMessageReceived) {
            Message m;
            m.sender = msg.senderName;  // Use display name, not MAC
            m.senderMAC = msg.senderMAC;  // Store MAC for sending receipts
            m.content = msg.content;
            m.timestamp = millis();
            m.received = true;
            m.status = MSG_RECEIVED;  // Start as received
            m.messageId = msg.messageId;
            onMessageReceived(m);
        }
    }
    
    // Check if we should forward (mesh networking)
    if (shouldForward(msg)) {
        Serial.println(F("[LoRa] Forwarding message..."));
        // TODO: Implement forwarding
    }
}

bool LoRaMessenger::shouldForward(const ParsedMessage& msg) {
    // Don't forward if hop limit reached
    if (msg.currentHop >= msg.maxHop) {
        return false;
    }
    
    // Don't forward ACK or read receipt messages
    if (msg.type == MSG_ACK || msg.type == MSG_READ_RECEIPT) {
        return false;
    }
    
    // Don't forward messages addressed directly to me
    if (msg.type == MSG_WHISPER && msg.target == String(myMAC, HEX)) {
        return false;
    }
    
    return true;
}
