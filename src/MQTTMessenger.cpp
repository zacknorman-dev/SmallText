#include "MQTTMessenger.h"
#include "Logger.h"

extern Logger logger;

// Static instance for callback
MQTTMessenger* MQTTMessenger::instance = nullptr;

MQTTMessenger::MQTTMessenger() : mqttClient(wifiClient) {
    encryption = nullptr;
    myVillageId = "";
    myVillageName = "";
    myUsername = "";
    myMAC = ESP.getEfuseMac();
    onMessageReceived = nullptr;
    onMessageAcked = nullptr;
    onMessageRead = nullptr;
    onCommandReceived = nullptr;
    lastReconnectAttempt = 0;
    lastPingTime = 0;
    lastSeenCleanup = 0;
    connected = false;
    
    // Generate unique client ID from MAC
    char macStr[13];
    sprintf(macStr, "%012llx", myMAC);
    clientId = "smoltxt_" + String(macStr);
    
    // Set static instance for callback
    instance = this;
    
    // Configure MQTT client
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);  // Increase buffer for encrypted messages
    mqttClient.setKeepAlive(60);
}

bool MQTTMessenger::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi not connected");
        return false;
    }
    
    Serial.println("[MQTT] Initializing MQTT messenger");
    Serial.println("[MQTT] Broker: " + String(MQTT_BROKER) + ":" + String(MQTT_PORT));
    Serial.println("[MQTT] Client ID: " + clientId);
    
    return reconnect();
}

void MQTTMessenger::setEncryption(Encryption* enc) {
    encryption = enc;
}

void MQTTMessenger::setVillageInfo(const String& villageId, const String& villageName, const String& username) {
    myVillageId = villageId;
    myVillageName = villageName;
    myUsername = username;
    Serial.println("[MQTT] Village Info Set:");
    Serial.println("  ID: " + myVillageId);
    Serial.println("  Name: " + myVillageName);
    Serial.println("  User: " + myUsername);
    
    // If already connected, resubscribe to new village topics
    if (isConnected()) {
        String baseTopic = "smoltxt/" + myVillageId + "/#";
        mqttClient.subscribe(baseTopic.c_str());
        Serial.println("[MQTT] Subscribed to: " + baseTopic);
    }
}

void MQTTMessenger::setMessageCallback(void (*callback)(const Message& msg)) {
    onMessageReceived = callback;
}

void MQTTMessenger::setAckCallback(void (*callback)(const String& messageId, const String& fromMAC)) {
    onMessageAcked = callback;
}

void MQTTMessenger::setReadCallback(void (*callback)(const String& messageId, const String& fromMAC)) {
    onMessageRead = callback;
}

void MQTTMessenger::setCommandCallback(void (*callback)(const String& command)) {
    onCommandReceived = callback;
}

String MQTTMessenger::generateMessageId() {
    static uint32_t counter = 0;
    counter++;
    char id[17];
    sprintf(id, "%08lx%08x", millis(), counter);
    return String(id);
}

String MQTTMessenger::generateTopic(const String& messageType, const String& target) {
    // Topic structure: smoltxt/{villageId}/{messageType}[/{target}]
    String topic = "smoltxt/" + myVillageId + "/" + messageType;
    if (!target.isEmpty()) {
        topic += "/" + target;
    }
    return topic;
}

bool MQTTMessenger::reconnect() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi not connected, can't reconnect");
        connected = false;
        return false;
    }
    
    // Limit reconnect attempts
    unsigned long now = millis();
    if (now - lastReconnectAttempt < 5000) {
        return false;  // Too soon
    }
    lastReconnectAttempt = now;
    
    Serial.print("[MQTT] Connecting to broker... ");
    
    if (mqttClient.connect(clientId.c_str())) {
        Serial.println("connected!");
        connected = true;
        
        // Subscribe to village topics
        if (!myVillageId.isEmpty()) {
            String baseTopic = "smoltxt/" + myVillageId + "/#";
            mqttClient.subscribe(baseTopic.c_str());
            Serial.println("[MQTT] Subscribed to: " + baseTopic);
            logger.info("MQTT: Connected and subscribed to " + myVillageName);
        }
        
        // Subscribe to device command topic (for remote control)
        char macStr[13];
        sprintf(macStr, "%012llx", myMAC);
        String commandTopic = "smoltxt/" + String(macStr) + "/command";
        mqttClient.subscribe(commandTopic.c_str());
        Serial.println("[MQTT] Subscribed to command topic: " + commandTopic);
        
        return true;
    } else {
        Serial.print("failed, rc=");
        Serial.println(mqttClient.state());
        connected = false;
        return false;
    }
}

void MQTTMessenger::loop() {
    // Maintain connection
    if (!mqttClient.connected()) {
        connected = false;
        reconnect();
    } else {
        mqttClient.loop();  // Process incoming messages
        connected = true;
    }
    
    // Cleanup old seen messages (every 5 minutes)
    unsigned long now = millis();
    if (now - lastSeenCleanup > 300000) {
        cleanupSeenMessages();
        lastSeenCleanup = now;
    }
}

void MQTTMessenger::cleanupSeenMessages() {
    // MQTT is more reliable than LoRa, keep seen messages for shorter time
    // For now, just clear all (in production, could use timestamp-based cleanup)
    if (seenMessageIds.size() > 100) {
        Serial.println("[MQTT] Clearing old seen message IDs (" + String(seenMessageIds.size()) + " entries)");
        seenMessageIds.clear();
    }
}

// Static callback - forwards to instance method
void MQTTMessenger::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    if (instance) {
        instance->handleIncomingMessage(String(topic), payload, length);
    }
}

void MQTTMessenger::handleIncomingMessage(const String& topic, const uint8_t* payload, unsigned int length) {
    Serial.println("[MQTT] Received on topic: " + topic);
    
    // Check if this is a command message
    char macStr[13];
    sprintf(macStr, "%012llx", myMAC);
    String commandTopic = "smoltxt/" + String(macStr) + "/command";
    
    if (topic == commandTopic) {
        // Command messages are plain text, not encrypted
        String command = "";
        for (unsigned int i = 0; i < length; i++) {
            command += (char)payload[i];
        }
        Serial.println("[MQTT] Received command: " + command);
        logger.info("MQTT command: " + command);
        
        if (onCommandReceived) {
            onCommandReceived(command);
        }
        return;
    }
    
    if (!encryption) {
        Serial.println("[MQTT] No encryption set, ignoring message");
        return;
    }
    
    // Decrypt payload
    String message;
    
    if (!encryption->decryptString(payload, length, message)) {
        Serial.println("[MQTT] Decryption failed");
        logger.error("MQTT: Decryption failed, len=" + String(length));
        return;
    }
    
    Serial.println("[MQTT] Decrypted: " + message);
    
    // Parse message (reuse LoRa format: TYPE:villageId:target:sender:senderMAC:msgId:content:hop:maxHop)
    ParsedMessage msg = parseMessage(message);
    
    if (msg.type == MSG_UNKNOWN) {
        Serial.println("[MQTT] Failed to parse message");
        return;
    }
    
    // Check if we've seen this message before
    if (seenMessageIds.find(msg.messageId) != seenMessageIds.end()) {
        Serial.println("[MQTT] Duplicate message, ignoring: " + msg.messageId);
        return;
    }
    seenMessageIds.insert(msg.messageId);
    
    // Normalize our MAC for comparison
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Handle ACK messages
    if (msg.type == MSG_ACK && msg.target == myMacStr) {
        Serial.println("[MQTT] Received ACK for message: " + msg.content);
        if (onMessageAcked) {
            onMessageAcked(msg.content, msg.senderMAC);
        }
        return;
    }
    
    // Handle read receipts
    if (msg.type == MSG_READ_RECEIPT && msg.target == myMacStr) {
        Serial.println("[MQTT] Received read receipt for message: " + msg.content);
        if (onMessageRead) {
            onMessageRead(msg.content, msg.senderMAC);
        }
        return;
    }
    
    // Handle regular messages (SHOUT or WHISPER)
    if (msg.type == MSG_SHOUT || (msg.type == MSG_WHISPER && msg.target == myMacStr)) {
        // Don't process our own messages
        if (msg.senderMAC == myMacStr) {
            Serial.println("[MQTT] Ignoring our own message");
            return;
        }
        
        // Send ACK
        Serial.println("[MQTT] Sending ACK for message: " + msg.messageId + " to " + msg.senderMAC);
        sendAck(msg.messageId, msg.senderMAC);
        
        // Deliver message to app
        if (onMessageReceived) {
            Message m;
            m.sender = msg.senderName;
            m.senderMAC = msg.senderMAC;
            m.content = msg.content;
            m.timestamp = millis();
            m.received = true;
            m.status = MSG_RECEIVED;
            m.messageId = msg.messageId;
            onMessageReceived(m);
        }
    }
}

ParsedMessage MQTTMessenger::parseMessage(const String& decrypted) {
    ParsedMessage msg;
    
    // Format: TYPE:villageId:target:sender:senderMAC:msgId:content:hop:maxHop
    int idx = 0;
    String parts[9];
    int partIdx = 0;
    
    for (int i = 0; i < decrypted.length() && partIdx < 9; i++) {
        if (decrypted[i] == ':') {
            partIdx++;
        } else {
            parts[partIdx] += decrypted[i];
        }
    }
    
    if (partIdx < 7) {
        return msg;  // Invalid format
    }
    
    // Parse type
    if (parts[0] == "SHOUT") msg.type = MSG_SHOUT;
    else if (parts[0] == "WHISPER") msg.type = MSG_WHISPER;
    else if (parts[0] == "ACK") msg.type = MSG_ACK;
    else if (parts[0] == "READ_RECEIPT") msg.type = MSG_READ_RECEIPT;
    else return msg;
    
    msg.villageId = parts[1];
    msg.target = parts[2];
    msg.senderName = parts[3];
    msg.senderMAC = parts[4];
    msg.messageId = parts[5];
    msg.content = parts[6];
    msg.currentHop = parts[7].toInt();
    msg.maxHop = parts[8].toInt();
    
    return msg;
}

String MQTTMessenger::sendShout(const String& message) {
    if (!isConnected() || !encryption) {
        Serial.println("[MQTT] Not connected or no encryption");
        return "";
    }
    
    String msgId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: SHOUT:villageId:*:sender:senderMAC:msgId:content:0:0
    String formatted = "SHOUT:" + myVillageId + ":*:" + myUsername + ":" + 
                      myMacStr + ":" + msgId + ":" + message + ":0:0";
    
    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        Serial.println("[MQTT] Encryption failed");
        return "";
    }
    
    // Publish to shout topic
    String topic = generateTopic("shout");
    bool success = mqttClient.publish(topic.c_str(), encrypted, encryptedLen);
    
    if (success) {
        Serial.println("[MQTT] SHOUT sent: " + message);
        logger.info("MQTT SHOUT sent: " + message);
        return msgId;
    } else {
        Serial.println("[MQTT] Publish failed");
        return "";
    }
}

String MQTTMessenger::sendWhisper(const String& recipientMAC, const String& message) {
    if (!isConnected() || !encryption) {
        Serial.println("[MQTT] Not connected or no encryption");
        return "";
    }
    
    String msgId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: WHISPER:villageId:recipientMAC:sender:senderMAC:msgId:content:0:0
    String formatted = "WHISPER:" + myVillageId + ":" + recipientMAC + ":" + 
                      myUsername + ":" + myMacStr + ":" + msgId + ":" + message + ":0:0";
    
    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        Serial.println("[MQTT] Encryption failed");
        return "";
    }
    
    // Publish to whisper topic for specific recipient
    String topic = generateTopic("whisper", recipientMAC);
    bool success = mqttClient.publish(topic.c_str(), encrypted, encryptedLen);
    
    if (success) {
        Serial.println("[MQTT] WHISPER sent to " + recipientMAC + ": " + message);
        logger.info("MQTT WHISPER sent: " + message);
        return msgId;
    } else {
        Serial.println("[MQTT] Publish failed");
        return "";
    }
}

bool MQTTMessenger::sendAck(const String& messageId, const String& targetMAC) {
    if (!isConnected() || !encryption) {
        return false;
    }
    
    String ackId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: ACK:villageId:targetMAC:sender:senderMAC:ackId:originalMessageId:0:0
    String formatted = "ACK:" + myVillageId + ":" + targetMAC + ":" + 
                      myUsername + ":" + myMacStr + ":" + ackId + ":" + messageId + ":0:0";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        return false;
    }
    
    String topic = generateTopic("ack", targetMAC);
    return mqttClient.publish(topic.c_str(), encrypted, encryptedLen);
}

bool MQTTMessenger::sendReadReceipt(const String& messageId, const String& targetMAC) {
    if (!isConnected() || !encryption) {
        return false;
    }
    
    String readId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: READ_RECEIPT:villageId:targetMAC:sender:senderMAC:readId:originalMessageId:0:0
    String formatted = "READ_RECEIPT:" + myVillageId + ":" + targetMAC + ":" + 
                      myUsername + ":" + myMacStr + ":" + readId + ":" + messageId + ":0:0";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        return false;
    }
    
    String topic = generateTopic("read", targetMAC);
    return mqttClient.publish(topic.c_str(), encrypted, encryptedLen);
}

String MQTTMessenger::getConnectionStatus() {
    if (WiFi.status() != WL_CONNECTED) {
        return "No WiFi";
    }
    
    if (!connected || !mqttClient.connected()) {
        return "Disconnected";
    }
    
    return "Connected";
}
