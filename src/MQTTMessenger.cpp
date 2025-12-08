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
    onSyncRequest = nullptr;
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

void MQTTMessenger::setSyncRequestCallback(void (*callback)(const String& requestorMAC, unsigned long timestamp)) {
    onSyncRequest = callback;
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
        
        // Subscribe to sync response topic (for receiving synced messages)
        String syncResponseTopic = "smoltxt/" + String(macStr) + "/sync-response";
        mqttClient.subscribe(syncResponseTopic.c_str());
        Serial.println("[MQTT] Subscribed to sync response topic: " + syncResponseTopic);
        
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
    
    // Check for sync-response topic (addressed to us)
    String syncResponseTopic = "smoltxt/" + String(macStr) + "/sync-response";
    if (topic == syncResponseTopic) {
        handleSyncResponse(payload, length);
        return;
    }
    
    // Check for sync-request topics (from other devices in our village)
    if (topic.startsWith("smoltxt/" + myVillageId + "/sync-request/")) {
        handleSyncRequest(payload, length);
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

bool MQTTMessenger::requestSync(unsigned long lastMessageTimestamp) {
    if (!connected || !mqttClient.connected()) {
        Serial.println("[MQTT] Cannot request sync - not connected");
        logger.error("Sync request failed: not connected");
        return false;
    }
    
    // Publish sync request to village topic
    // Timestamp ignored - will send all messages and rely on deduplication
    // Format: sync-request/{deviceMAC}
    // Payload: {mac: myMAC}
    
    JsonDocument doc;
    doc["mac"] = String(myMAC, HEX);
    doc["timestamp"] = lastMessageTimestamp;  // Keep for backwards compat but ignored
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println("[MQTT] Sync request payload: " + payload);
    
    // Encrypt the sync request
    uint8_t encrypted[256];
    int encryptedLen = encryption->encrypt((uint8_t*)payload.c_str(), payload.length(), encrypted, sizeof(encrypted));
    
    if (encryptedLen <= 0) {
        Serial.println("[MQTT] Sync request encryption failed");
        logger.error("Sync encryption failed");
        return false;
    }
    
    String topic = "smoltxt/" + myVillageId + "/sync-request/" + String(myMAC, HEX);
    Serial.println("[MQTT] Publishing sync request to: " + topic);
    bool success = mqttClient.publish(topic.c_str(), encrypted, encryptedLen);
    
    if (success) {
        Serial.println("[MQTT] Sync request sent (will receive all messages, dedup on receive)");
        logger.info("Sync request sent");
    } else {
        Serial.println("[MQTT] Sync request failed");
        logger.error("Sync request publish failed");
    }
    
    return success;
}

bool MQTTMessenger::sendSyncResponse(const String& targetMAC, const std::vector<Message>& messages) {
    if (!connected || !mqttClient.connected()) {
        Serial.println("[MQTT] Cannot send sync response - not connected");
        return false;
    }
    
    if (messages.empty()) {
        Serial.println("[MQTT] No messages to sync");
        return true;
    }
    
    Serial.println("[MQTT] Sending sync response with " + String(messages.size()) + " messages to " + targetMAC);
    logger.info("Sync response: " + String(messages.size()) + " msgs to " + targetMAC);
    
    // Send messages in batches to avoid payload size limits
    const int BATCH_SIZE = 1;  // Reduced to 1 to ensure delivery
    int totalSent = 0;
    
    for (size_t i = 0; i < messages.size(); i += BATCH_SIZE) {
        JsonDocument doc;
        JsonArray msgArray = doc["messages"].to<JsonArray>();
        
        // Add up to BATCH_SIZE messages
        for (size_t j = i; j < messages.size() && j < i + BATCH_SIZE; j++) {
            JsonObject msgObj = msgArray.add<JsonObject>();
            msgObj["sender"] = messages[j].sender;
            msgObj["senderMAC"] = messages[j].senderMAC;
            msgObj["content"] = messages[j].content;
            msgObj["timestamp"] = messages[j].timestamp;
            msgObj["messageId"] = messages[j].messageId;
            msgObj["received"] = messages[j].received;
            msgObj["status"] = (int)messages[j].status;
        }
        
        doc["batch"] = (i / BATCH_SIZE) + 1;
        doc["total"] = (messages.size() + BATCH_SIZE - 1) / BATCH_SIZE;
        
        String payload;
        serializeJson(doc, payload);
        
        // Encrypt
        uint8_t encrypted[512];
        int encryptedLen = encryption->encrypt((uint8_t*)payload.c_str(), payload.length(), encrypted, sizeof(encrypted));
        
        if (encryptedLen <= 0) {
            Serial.println("[MQTT] Sync response encryption failed");
            return false;
        }
        
        String topic = "smoltxt/" + targetMAC + "/sync-response";
        bool success = mqttClient.publish(topic.c_str(), encrypted, encryptedLen);
        
        if (success) {
            totalSent += min(BATCH_SIZE, (int)(messages.size() - i));
            Serial.println("[MQTT] Sent sync batch " + String((i / BATCH_SIZE) + 1) + " (" + String(totalSent) + "/" + String(messages.size()) + " messages)");
            logger.info("Sync batch " + String((i / BATCH_SIZE) + 1) + " sent");
            delay(100);  // Brief delay between batches
        } else {
            Serial.println("[MQTT] Sync batch failed");
            logger.error("Sync batch " + String((i / BATCH_SIZE) + 1) + " failed");
            return false;
        }
    }
    
    return true;
}

void MQTTMessenger::handleSyncRequest(const uint8_t* payload, unsigned int length) {
    Serial.println("[MQTT] Received sync request, decrypting...");
    
    if (!encryption) {
        Serial.println("[MQTT] No encryption set");
        return;
    }
    
    String message;
    if (!encryption->decryptString(payload, length, message)) {
        Serial.println("[MQTT] Sync request decryption failed");
        logger.error("Sync request decrypt failed");
        return;
    }
    
    Serial.println("[MQTT] Decrypted sync request: " + message);
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.println("[MQTT] Sync request parse error: " + String(error.c_str()));
        logger.error("Sync request JSON error");
        return;
    }
    
    unsigned long requestedTimestamp = doc["timestamp"] | 0;
    String requestorMAC = doc["mac"] | "";
    
    Serial.println("[MQTT] Sync request from " + requestorMAC + " for messages after timestamp " + String(requestedTimestamp));
    logger.info("Sync from " + requestorMAC + " ts=" + String(requestedTimestamp));
    
    // Trigger callback to main app to handle sync
    if (onSyncRequest) {
        onSyncRequest(requestorMAC, requestedTimestamp);
    } else {
        Serial.println("[MQTT] No sync request callback set!");
    }
}

void MQTTMessenger::handleSyncResponse(const uint8_t* payload, unsigned int length) {
    Serial.println("[MQTT] Received sync response, decrypting...");
    
    if (!encryption) {
        Serial.println("[MQTT] No encryption set");
        return;
    }
    
    String message;
    if (!encryption->decryptString(payload, length, message)) {
        Serial.println("[MQTT] Sync response decryption failed");
        logger.error("Sync response decrypt failed");
        return;
    }
    
    Serial.println("[MQTT] Decrypted sync response: " + message.substring(0, 100) + "...");
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.println("[MQTT] Sync response parse error: " + String(error.c_str()));
        logger.error("Sync response JSON error");
        return;
    }
    
    int batch = doc["batch"] | 0;
    int total = doc["total"] | 0;
    
    Serial.println("[MQTT] Sync response batch " + String(batch) + "/" + String(total));
    logger.info("Sync batch " + String(batch) + "/" + String(total));
    
    // OPTIMIZATION: Set global sync flag to skip expensive status updates during sync
    // This is a global from main.cpp - forward declaration needed
    extern bool isSyncing;
    if (batch == 1) {
        isSyncing = true;  // Start of sync
        Serial.println("[MQTT] Sync started - disabling status updates");
    }
    
    JsonArray msgArray = doc["messages"];
    int msgCount = 0;
    
    for (JsonObject msgObj : msgArray) {
        Message msg;
        msg.sender = msgObj["sender"] | "";
        msg.senderMAC = msgObj["senderMAC"] | "";
        msg.content = msgObj["content"] | "";
        msg.timestamp = msgObj["timestamp"] | 0;
        msg.messageId = msgObj["messageId"] | "";
        msg.received = msgObj["received"] | true;
        msg.status = (MessageStatus)(msgObj["status"] | MSG_RECEIVED);
        
        // Deliver to app via message callback (deduplication happens in Village::saveMessage)
        if (onMessageReceived) {
            Serial.println("[MQTT] Synced message: " + msg.messageId + " from " + msg.sender);
            onMessageReceived(msg);
            msgCount++;
        }
    }
    
    // End of sync - re-enable status updates
    if (batch == total) {
        isSyncing = false;
        Serial.println("[MQTT] Sync completed - re-enabled status updates");
    }
    
    Serial.println("[MQTT] Processed " + String(msgCount) + " synced messages");
    logger.info("Synced " + String(msgCount) + " messages");
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
