#include "MQTTMessenger.h"
#include "Logger.h"

extern Logger logger;

// Static instance for callback
MQTTMessenger* MQTTMessenger::instance = nullptr;

MQTTMessenger::MQTTMessenger() : mqttClient(wifiClient) {
    encryption = nullptr;
    currentVillageId = "";
    currentVillageName = "";
    currentUsername = "";
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
    currentSyncPhase = 0;  // Not syncing
    syncTargetMAC = "";
    lastSyncPhaseTime = 0;
    
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
    currentVillageId = villageId;
    currentVillageName = villageName;
    currentUsername = username;
    Serial.println("[MQTT] Active Village Set:");
    Serial.println("  ID: " + currentVillageId);
    Serial.println("  Name: " + currentVillageName);
    Serial.println("  User: " + currentUsername);
    
    // For backwards compatibility: if this village isn't in subscriptions, add it
    if (!findVillageSubscription(villageId) && encryption) {
        addVillageSubscription(villageId, villageName, username, encryption->getKey());
    } else {
        // Just set it as active
        setActiveVillage(villageId);
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

void MQTTMessenger::setVillageNameCallback(void (*callback)(const String& villageId, const String& villageName)) {
    onVillageNameReceived = callback;
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
    String topic = "smoltxt/" + currentVillageId + "/" + messageType;
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
    
    // Connect with persistent session (cleanSession=false) to enable message queueing while offline
    if (mqttClient.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, false)) {
        Serial.println("connected with persistent session!");
        connected = true;
        
        // Subscribe to all saved villages
        if (subscribedVillages.size() > 0) {
            for (const auto& village : subscribedVillages) {
                String baseTopic = "smoltxt/" + village.villageId + "/#";
                mqttClient.subscribe(baseTopic.c_str());
                Serial.println("[MQTT] Subscribed to: " + baseTopic + " (" + village.villageName + ")");
            }
            logger.info("MQTT: Connected - subscribed to " + String(subscribedVillages.size()) + " villages");
        } else {
            Serial.println("[MQTT] Warning: No villages to subscribe to");
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
    
    // Background sync phase continuation
    // If Phase 1 is complete and more history exists, request next phase after delay
    unsigned long now = millis();
    if (currentSyncPhase > 1 && !syncTargetMAC.isEmpty()) {
        // Wait 5 seconds between background phases to not overwhelm the device
        if (now - lastSyncPhaseTime > 5000) {
            Serial.println("[MQTT] Requesting background sync Phase " + String(currentSyncPhase));
            
            // Send sync request with phase number (using timestamp field as hack)
            requestSync(currentSyncPhase);  // Phase number passed as timestamp
            
            // Reset state so we don't spam requests
            syncTargetMAC = "";
            currentSyncPhase = 0;
        }
    }
    
    // Cleanup old seen messages (every 5 minutes)
    if (now - lastSeenCleanup > 300000) {
        cleanupSeenMessages();
        lastSeenCleanup = now;
    }
}

void MQTTMessenger::cleanupSeenMessages() {
    // Keep seen messages for deduplication
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
    
    // Extract villageId from topic: smoltxt/{villageId}/...
    int firstSlash = topic.indexOf('/');
    int secondSlash = topic.indexOf('/', firstSlash + 1);
    if (firstSlash == -1 || secondSlash == -1) {
        Serial.println("[MQTT] Invalid topic format");
        return;
    }
    
    String villageId = topic.substring(firstSlash + 1, secondSlash);
    Serial.println("[MQTT] Message for village: " + villageId);
    
    // Check for village name announcement (unencrypted, just the name)
    if (topic.endsWith("/villagename")) {
        String villageName = "";
        for (unsigned int i = 0; i < length; i++) {
            villageName += (char)payload[i];
        }
        Serial.println("[MQTT] Received village name announcement: " + villageName + " for village: " + villageId);
        logger.info("Village name received: " + villageName + " (ID: " + villageId + ")");
        
        if (onVillageNameReceived) {
            onVillageNameReceived(villageId, villageName);
        }
        return;
    }
    
    // Check for sync-request topics (from other devices)
    if (topic.startsWith("smoltxt/" + villageId + "/sync-request/")) {
        handleSyncRequest(payload, length);
        return;
    }
    
    // Find the village subscription to get the encryption key
    VillageSubscription* village = findVillageSubscription(villageId);
    if (!village) {
        Serial.println("[MQTT] Village not found in subscriptions: " + villageId);
        return;
    }
    
    // Create temporary encryption object with this village's key
    Encryption tempEncryption;
    tempEncryption.setKey(village->encryptionKey);
    
    // Decrypt payload
    String message;
    
    if (!tempEncryption.decryptString(payload, length, message)) {
        Serial.println("[MQTT] Decryption failed for village: " + village->villageName);
        logger.error("MQTT: Decryption failed for " + village->villageName);
        return;
    }
    
    Serial.println("[MQTT] Decrypted message from " + village->villageName + ": " + message);
    
    // Parse message format: TYPE:villageId:target:sender:senderMAC:msgId:content:hop:maxHop
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
            extern unsigned long getCurrentTime();
            
            // Find which village this message is for
            VillageSubscription* msgVillage = findVillageSubscription(msg.villageId);
            
            Message m;
            m.sender = msg.senderName;
            m.senderMAC = msg.senderMAC;
            m.content = msg.content;
            m.timestamp = getCurrentTime();
            m.villageId = msg.villageId;  // Pass village ID for multi-village support
            
            // Determine if this is a received message or our own sent message
            // Compare sender username with this village's username
            if (msgVillage && msg.senderName == msgVillage->username) {
                // This is OUR message (synced back from another device)
                m.received = false;
                m.status = MSG_SENT;  // Our sent messages start as MSG_SENT
                Serial.println("[MQTT] Received our own sent message: " + msg.messageId);
            } else {
                // This is someone else's message
                m.received = true;
                m.status = MSG_RECEIVED;
                Serial.println("[MQTT] Received message from " + msg.senderName);
            }
            
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

bool MQTTMessenger::announceVillageName(const String& villageName) {
    if (!isConnected() || currentVillageId.isEmpty()) {
        Serial.println("[MQTT] Cannot announce: not connected or no active village");
        return false;
    }
    
    // Topic: smoltxt/{villageId}/villagename
    String topic = "smoltxt/" + currentVillageId + "/villagename";
    
    // Payload is just the village name (no encryption needed - derived from same password)
    if (mqttClient.publish(topic.c_str(), villageName.c_str())) {
        Serial.println("[MQTT] Village name announced: " + villageName);
        return true;
    }
    
    Serial.println("[MQTT] Failed to announce village name");
    return false;
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
    String formatted = "SHOUT:" + currentVillageId + ":*:" + currentUsername + ":" + 
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
    String formatted = "WHISPER:" + currentVillageId + ":" + recipientMAC + ":" +
                      currentUsername + ":" + myMacStr + ":" + msgId + ":" + message + ":0:0";    // Encrypt
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
    String formatted = "ACK:" + currentVillageId + ":" + targetMAC + ":" +
                      currentUsername + ":" + myMacStr + ":" + ackId + ":" + messageId + ":0:0";    uint8_t encrypted[MAX_CIPHERTEXT];
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
    String formatted = "READ_RECEIPT:" + currentVillageId + ":" + targetMAC + ":" +
                      currentUsername + ":" + myMacStr + ":" + readId + ":" + messageId + ":0:0";    uint8_t encrypted[MAX_CIPHERTEXT];
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
    
    String topic = "smoltxt/" + currentVillageId + "/sync-request/" + String(myMAC, HEX);
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

bool MQTTMessenger::sendSyncResponse(const String& targetMAC, const std::vector<Message>& messages, int phase) {
    if (!connected || !mqttClient.connected()) {
        Serial.println("[MQTT] Cannot send sync response - not connected");
        return false;
    }
    
    if (messages.empty()) {
        Serial.println("[MQTT] No messages to sync");
        return true;
    }
    
    // BATCHED SYNC: Send only 20 messages per phase, starting with most recent
    const int MESSAGES_PER_PHASE = 20;
    int totalMessages = messages.size();
    
    // Calculate which 20 messages to send for this phase
    // Phase 1: Last 20 (most recent)
    // Phase 2: Messages 21-40
    // Phase 3: Messages 41-60, etc.
    int startIdx = max(0, totalMessages - (phase * MESSAGES_PER_PHASE));
    int endIdx = totalMessages - ((phase - 1) * MESSAGES_PER_PHASE);
    
    // Extract the slice for this phase
    std::vector<Message> phaseMessages;
    for (int i = startIdx; i < endIdx && i < totalMessages; i++) {
        phaseMessages.push_back(messages[i]);
    }
    
    if (phaseMessages.empty()) {
        Serial.println("[MQTT] Phase " + String(phase) + " complete - no more messages");
        logger.info("Sync phase " + String(phase) + " complete");
        return true;
    }
    
    Serial.println("[MQTT] Sync Phase " + String(phase) + ": Sending " + String(phaseMessages.size()) + " messages (" + String(startIdx) + "-" + String(endIdx-1) + " of " + String(totalMessages) + ") to " + targetMAC);
    logger.info("Sync phase " + String(phase) + ": " + String(phaseMessages.size()) + " msgs");
    
    // Send messages in batches to avoid payload size limits
    const int BATCH_SIZE = 1;  // Reduced to 1 to ensure delivery
    int totalSent = 0;
    
    for (size_t i = 0; i < phaseMessages.size(); i += BATCH_SIZE) {
        JsonDocument doc;
        JsonArray msgArray = doc["messages"].to<JsonArray>();
        
        // Add up to BATCH_SIZE messages
        for (size_t j = i; j < phaseMessages.size() && j < i + BATCH_SIZE; j++) {
            JsonObject msgObj = msgArray.add<JsonObject>();
            msgObj["sender"] = phaseMessages[j].sender;
            msgObj["senderMAC"] = phaseMessages[j].senderMAC;
            msgObj["content"] = phaseMessages[j].content;
            msgObj["timestamp"] = phaseMessages[j].timestamp;
            msgObj["messageId"] = phaseMessages[j].messageId;
            msgObj["received"] = phaseMessages[j].received;
            msgObj["status"] = (int)phaseMessages[j].status;
            msgObj["villageId"] = phaseMessages[j].villageId;  // Include village ID for multi-village support
        }
        
        doc["batch"] = (i / BATCH_SIZE) + 1;
        doc["total"] = (phaseMessages.size() + BATCH_SIZE - 1) / BATCH_SIZE;
        doc["phase"] = phase;  // Include phase number in payload
        doc["morePhases"] = (startIdx > 0);  // Indicate if more history available
        
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
            totalSent += min(BATCH_SIZE, (int)(phaseMessages.size() - i));
            Serial.println("[MQTT] Phase " + String(phase) + " batch " + String((i / BATCH_SIZE) + 1) + "/" + String((phaseMessages.size() + BATCH_SIZE - 1) / BATCH_SIZE) + " sent");
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
    int phase = doc["phase"] | 1;
    bool morePhases = doc["morePhases"] | false;
    
    Serial.println("[MQTT] Sync phase " + String(phase) + " batch " + String(batch) + "/" + String(total));
    logger.info("Sync phase " + String(phase) + " batch " + String(batch) + "/" + String(total));
    
    // OPTIMIZATION: Set global sync flag to skip expensive status updates during sync
    // This is a global from main.cpp - forward declaration needed
    extern bool isSyncing;
    if (batch == 1 && phase == 1) {
        isSyncing = true;  // Start of sync
        currentSyncPhase = 1;
        Serial.println("[MQTT] Sync Phase 1 started (recent 20 messages) - disabling status updates");
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
        msg.villageId = msgObj["villageId"] | "";  // Extract village ID from sync response
        
        // Store sender MAC from first message for background sync continuation
        if (msgCount == 0 && batch == 1 && phase == 1 && !msg.senderMAC.isEmpty()) {
            syncTargetMAC = msg.senderMAC;
            Serial.println("[MQTT] Stored sync target MAC: " + syncTargetMAC + " for background phases");
        }
        
        // Deliver to app via message callback (deduplication happens in Village::saveMessage)
        if (onMessageReceived) {
            Serial.println("[MQTT] Synced message: " + msg.messageId + " from " + msg.sender);
            onMessageReceived(msg);
            msgCount++;
        }
    }
    
    // End of phase
    if (batch == total) {
        Serial.println("[MQTT] Phase " + String(phase) + " complete - processed " + String(msgCount) + " messages");
        
        // ===== SYNC DEBUG: Trigger message store dump after phase completes =====
        extern void dumpMessageStoreDebug(int completedPhase);
        dumpMessageStoreDebug(phase);
        
        if (phase == 1) {
            // Phase 1 complete - re-enable status updates, user has recent messages
            isSyncing = false;
            Serial.println("[MQTT] Phase 1 complete - recent messages synced, re-enabled status updates");
            logger.info("Phase 1 complete: " + String(msgCount) + " recent msgs");
            
            // Store sync state for background phase continuation
            if (morePhases) {
                currentSyncPhase = 2;
                lastSyncPhaseTime = millis();
                Serial.println("[MQTT] More history available - will request Phase 2 in background after delay");
            } else {
                currentSyncPhase = 0;  // All done
                Serial.println("[MQTT] Sync fully complete - no more history");
            }
        } else {
            // Background phase complete
            Serial.println("[MQTT] Background phase " + String(phase) + " complete");
            logger.info("Phase " + String(phase) + " complete: " + String(msgCount) + " msgs");
            
            if (morePhases) {
                currentSyncPhase = phase + 1;
                lastSyncPhaseTime = millis();
                Serial.println("[MQTT] Will request Phase " + String(currentSyncPhase) + " in background");
            } else {
                currentSyncPhase = 0;  // All history synced
                Serial.println("[MQTT] All history synced");
            }
        }
    }
    
    Serial.println("[MQTT] Processed " + String(msgCount) + " synced messages in phase " + String(phase));
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

// Multi-village subscription management

void MQTTMessenger::addVillageSubscription(const String& villageId, const String& villageName, const String& username, const uint8_t* encKey) {
    // Check if already subscribed
    for (auto& village : subscribedVillages) {
        if (village.villageId == villageId) {
            Serial.println("[MQTT] Village already subscribed: " + villageName);
            return;
        }
    }
    
    // Add new subscription
    VillageSubscription sub;
    sub.villageId = villageId;
    sub.villageName = villageName;
    sub.username = username;
    memcpy(sub.encryptionKey, encKey, 32);
    subscribedVillages.push_back(sub);
    
    Serial.println("[MQTT] Added village subscription: " + villageName + " (" + villageId + ")");
    
    // Subscribe to MQTT topic if already connected
    if (isConnected()) {
        String baseTopic = "smoltxt/" + villageId + "/#";
        mqttClient.subscribe(baseTopic.c_str());
        Serial.println("[MQTT] Subscribed to topic: " + baseTopic);
    }
}

void MQTTMessenger::removeVillageSubscription(const String& villageId) {
    for (auto it = subscribedVillages.begin(); it != subscribedVillages.end(); ++it) {
        if (it->villageId == villageId) {
            Serial.println("[MQTT] Removing village subscription: " + it->villageName);
            
            // Unsubscribe from MQTT topic if connected
            if (isConnected()) {
                String baseTopic = "smoltxt/" + villageId + "/#";
                mqttClient.unsubscribe(baseTopic.c_str());
                Serial.println("[MQTT] Unsubscribed from topic: " + baseTopic);
            }
            
            subscribedVillages.erase(it);
            return;
        }
    }
}

void MQTTMessenger::setActiveVillage(const String& villageId) {
    VillageSubscription* village = findVillageSubscription(villageId);
    if (village) {
        currentVillageId = village->villageId;
        currentVillageName = village->villageName;
        currentUsername = village->username;
        
        // Update encryption key for sending
        if (encryption) {
            encryption->setKey(village->encryptionKey);
        }
        
        Serial.println("[MQTT] Active village set to: " + currentVillageName);
    } else {
        Serial.println("[MQTT] Warning: Village not found: " + villageId);
    }
}

void MQTTMessenger::subscribeToAllVillages() {
    Serial.println("[MQTT] Scanning for saved villages...");
    
    // Clear existing subscriptions
    subscribedVillages.clear();
    
    // Scan all village slots (0-9)
    for (int slot = 0; slot < 10; slot++) {
        if (Village::hasVillageInSlot(slot)) {
            Village tempVillage;
            if (tempVillage.loadFromSlot(slot)) {
                addVillageSubscription(
                    tempVillage.getVillageId(),
                    tempVillage.getVillageName(),
                    tempVillage.getUsername(),
                    tempVillage.getEncryptionKey()
                );
            }
        }
    }
    
    Serial.println("[MQTT] Subscribed to " + String(subscribedVillages.size()) + " villages");
    logger.info("MQTT: Subscribed to " + String(subscribedVillages.size()) + " villages");
}

VillageSubscription* MQTTMessenger::findVillageSubscription(const String& villageId) {
    for (auto& village : subscribedVillages) {
        if (village.villageId == villageId) {
            return &village;
        }
    }
    return nullptr;
}

