# AsyncMqttClient Implementation Backup
**Date:** December 9, 2025  
**Version:** v0.36.3  
**Purpose:** Reference document before migrating to ESP-MQTT library for TLS support

## Overview
This document captures the complete AsyncMqttClient implementation that works correctly with test.mosquitto.org. Use this as reference when migrating to ESP-MQTT to ensure all functionality is preserved.

---

## Critical Implementation Details

### 1. QoS 1 Configuration
**All MQTT operations use QoS 1** for guaranteed delivery with offline message queuing:

```cpp
// Publishing (returns packet ID, not boolean)
uint16_t packetId = mqttClient.publish(topic.c_str(), 1, false, payload, length);
// Parameters: topic, QoS=1, retain=false, payload, length

// Subscribing
mqttClient.subscribe(topic.c_str(), 1);
// Parameters: topic, QoS=1
```

### 2. Persistent Sessions
```cpp
mqttClient.setCleanSession(false);  // CRITICAL for offline message queuing
```
- Client ID must be consistent across reconnections: `"smoltxt_" + MAC address`
- Broker maintains subscriptions and queued messages when device is offline

### 3. Callback Architecture
AsyncMqttClient uses **static callbacks** that forward to instance methods:

```cpp
static MQTTMessenger* instance;  // Static pointer to singleton

// Set callbacks in constructor
mqttClient.onConnect(onMqttConnect);
mqttClient.onDisconnect(onMqttDisconnect);
mqttClient.onMessage(onMqttMessage);

// Static callbacks
static void onMqttConnect(bool sessionPresent) {
    if (!instance) return;
    instance->connected = true;
    // Subscribe to all villages with QoS 1
}

static void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    if (!instance) return;
    instance->connected = false;
}

static void onMqttMessage(char* topic, char* payload, 
                         AsyncMqttClientMessageProperties properties,
                         size_t len, size_t index, size_t total) {
    if (!instance) return;
    instance->handleIncomingMessage(String(topic), (uint8_t*)payload, len);
}
```

### 4. Connection Management
```cpp
bool reconnect() {
    if (WiFi.status() != WL_CONNECTED) return false;
    
    unsigned long now = millis();
    if (now - lastReconnectAttempt < 5000) return false;  // Rate limit
    lastReconnectAttempt = now;
    
    mqttClient.connect();  // Asynchronous - callback handles subscriptions
    return true;
}

void loop() {
    // AsyncMqttClient handles connection automatically
    if (!mqttClient.connected()) {
        connected = false;
        reconnect();
    } else {
        connected = true;
    }
    // No mqttClient.loop() needed - library is fully asynchronous
}
```

---

## Message Flow Patterns

### Sending Messages (SHOUT)
```cpp
String sendShout(const String& message) {
    String msgId = generateMessageId();
    String myMacStr = String(myMAC, HEX).toLowerCase();
    
    // Format: SHOUT:villageId:*:sender:senderMAC:msgId:content:0:0
    String formatted = "SHOUT:" + currentVillageId + ":*:" + currentUsername + ":" + 
                      myMacStr + ":" + msgId + ":" + message + ":0:0";
    
    // Encrypt with village key
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen);
    
    // Publish with QoS 1
    String topic = "smoltxt/" + currentVillageId + "/shout";
    uint16_t packetId = mqttClient.publish(topic.c_str(), 1, false, 
                                           (const char*)encrypted, encryptedLen);
    
    return (packetId != 0) ? msgId : "";
}
```

### Receiving Messages
```cpp
void handleIncomingMessage(const String& topic, const uint8_t* payload, unsigned int length) {
    // Extract villageId from topic: smoltxt/{villageId}/...
    int firstSlash = topic.indexOf('/');
    int secondSlash = topic.indexOf('/', firstSlash + 1);
    String villageId = topic.substring(firstSlash + 1, secondSlash);
    
    // Find village subscription (for encryption key)
    VillageSubscription* village = findVillageSubscription(villageId);
    if (!village) return;
    
    // Decrypt with village key
    Encryption tempEncryption;
    tempEncryption.setKey(village->encryptionKey);
    String message;
    if (!tempEncryption.decryptString(payload, length, message)) return;
    
    // Parse and route message
    ParsedMessage msg = parseMessage(message);
    
    // Check for duplicates
    if (seenMessageIds.find(msg.messageId) != seenMessageIds.end()) return;
    seenMessageIds.insert(msg.messageId);
    
    // Route based on type
    if (msg.type == MSG_ACK) { /* Handle ACK */ }
    else if (msg.type == MSG_READ_RECEIPT) { /* Handle read receipt */ }
    else if (msg.type == MSG_SHOUT) { 
        sendAck(msg.messageId, msg.senderMAC);  // Auto-ACK
        onMessageReceived(convertToMessage(msg));  // Deliver to app
    }
}
```

### ACK Flow
```cpp
bool sendAck(const String& messageId, const String& targetMAC) {
    String ackId = generateMessageId();
    String myMacStr = String(myMAC, HEX).toLowerCase();
    
    // Format: ACK:villageId:targetMAC:sender:senderMAC:ackId:originalMessageId:0:0
    String formatted = "ACK:" + currentVillageId + ":" + targetMAC + ":" +
                      currentUsername + ":" + myMacStr + ":" + ackId + ":" + 
                      messageId + ":0:0";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen);
    
    String topic = "smoltxt/" + currentVillageId + "/ack/" + targetMAC;
    return mqttClient.publish(topic.c_str(), 1, false, 
                             (const char*)encrypted, encryptedLen) != 0;
}
```

### Read Receipt Flow
```cpp
bool sendReadReceipt(const String& messageId, const String& targetMAC) {
    String readId = generateMessageId();
    String myMacStr = String(myMAC, HEX).toLowerCase();
    
    // Format: READ_RECEIPT:villageId:targetMAC:sender:senderMAC:readId:originalMessageId:0:0
    String formatted = "READ_RECEIPT:" + currentVillageId + ":" + targetMAC + ":" +
                      currentUsername + ":" + myMacStr + ":" + readId + ":" + 
                      messageId + ":0:0";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen);
    
    String topic = "smoltxt/" + currentVillageId + "/read/" + targetMAC;
    return mqttClient.publish(topic.c_str(), 1, false, 
                             (const char*)encrypted, encryptedLen) != 0;
}
```

---

## Multi-Village Support

### Village Subscription Structure
```cpp
struct VillageSubscription {
    String villageId;
    String villageName;
    String username;
    uint8_t encryptionKey[32];  // ChaCha20 key
};

std::vector<VillageSubscription> subscribedVillages;
```

### Subscribe to All Villages
```cpp
void onMqttConnect(bool sessionPresent) {
    if (subscribedVillages.size() > 0) {
        for (const auto& village : subscribedVillages) {
            // Subscribe to all message types for this village
            mqttClient.subscribe(("smoltxt/" + village.villageId + "/shout").c_str(), 1);
            mqttClient.subscribe(("smoltxt/" + village.villageId + "/ack/" + myMacStr).c_str(), 1);
            mqttClient.subscribe(("smoltxt/" + village.villageId + "/read/" + myMacStr).c_str(), 1);
            mqttClient.subscribe(("smoltxt/" + village.villageId + "/villagename").c_str(), 1);
            mqttClient.subscribe(("smoltxt/" + village.villageId + "/sync-request/*").c_str(), 1);
        }
    }
}
```

### Message Routing to Correct Village
```cpp
void handleIncomingMessage(const String& topic, const uint8_t* payload, unsigned int length) {
    // Extract village ID from topic
    String villageId = /* parsed from topic */;
    
    // Find matching village subscription (has encryption key)
    VillageSubscription* village = findVillageSubscription(villageId);
    
    // Decrypt with this village's key
    Encryption tempEncryption;
    tempEncryption.setKey(village->encryptionKey);
    // ... decrypt and process
}
```

---

## Sync Protocol (for Offline Devices)

### Request Sync
```cpp
bool requestSync(unsigned long lastMessageTimestamp) {
    JsonDocument doc;
    doc["mac"] = String(myMAC, HEX);
    doc["timestamp"] = lastMessageTimestamp;
    
    String payload;
    serializeJson(doc, payload);
    
    // Encrypt sync request
    uint8_t encrypted[256];
    int encryptedLen = encryption->encrypt((uint8_t*)payload.c_str(), 
                                          payload.length(), encrypted, sizeof(encrypted));
    
    String topic = "smoltxt/" + currentVillageId + "/sync-request/" + String(myMAC, HEX);
    uint16_t packetId = mqttClient.publish(topic.c_str(), 1, false, 
                                           (const char*)encrypted, encryptedLen);
    return (packetId != 0);
}
```

### Send Sync Response
```cpp
bool sendSyncResponse(const String& targetMAC, const std::vector<Message>& messages, int phase) {
    // Batch messages (20 per phase) to avoid payload limits
    const int MESSAGES_PER_PHASE = 20;
    
    JsonDocument doc;
    doc["phase"] = phase;
    doc["batch"] = /* current batch */;
    doc["total"] = /* total batches */;
    
    JsonArray msgArray = doc["messages"].to<JsonArray>();
    for (const Message& msg : phaseMessages) {
        JsonObject msgObj = msgArray.add<JsonObject>();
        msgObj["id"] = msg.messageId;
        msgObj["sender"] = msg.sender;
        msgObj["senderMAC"] = msg.senderMAC;
        msgObj["content"] = msg.content;
        msgObj["timestamp"] = msg.timestamp;
        msgObj["status"] = (int)msg.status;
    }
    
    // Encrypt and send
    String topic = "smoltxt/" + targetMAC + "/sync-response";
    // ... encrypt and publish with QoS 1
}
```

---

## Topic Structure Reference

```
smoltxt/{villageId}/shout              - Broadcast messages (QoS 1)
smoltxt/{villageId}/whisper/{mac}      - Direct messages (QoS 1)
smoltxt/{villageId}/ack/{targetMAC}    - Delivery acknowledgments (QoS 1)
smoltxt/{villageId}/read/{targetMAC}   - Read receipts (QoS 1)
smoltxt/{villageId}/villagename        - Village name announcements (QoS 1, retained)
smoltxt/{villageId}/sync-request/{mac} - Sync requests (QoS 1)
smoltxt/{deviceMAC}/sync-response      - Sync responses (QoS 1)
smoltxt/{deviceMAC}/command            - Device commands (QoS 1)
```

---

## Message Status Flow

### Status Values
```cpp
enum MessageStatus {
    MSG_SENT = 1,      // ✓ - Sent successfully
    MSG_RECEIVED = 2,  // ✓✓ - Delivered to recipient
    MSG_READ = 3       // ✓✓✓ - Viewed by recipient
};
```

### Status Update Order (CRITICAL)
```cpp
// In main.cpp - onMessageReceived()
// 1. Save incoming message with initial status
village.saveMessage(msg);

// 2. Mark as received (status 2) for ALL incoming messages
if (!isSyncing && msg.received && msg.status == MSG_RECEIVED) {
    village.updateMessageStatus(msg.messageId, MSG_RECEIVED);
}

// 3. If viewing messaging screen, upgrade to read (status 3)
if (!isSyncing && msg.received && appState == APP_MESSAGING && inMessagingScreen) {
    village.updateMessageStatus(msg.messageId, MSG_READ);
    ui.updateMessageStatus(msg.messageId, MSG_READ);
}

// In main.cpp - onMessageAcked() - Race condition fix
// 1. Update storage FIRST (source of truth)
if (!isSyncing) {
    village.updateMessageStatus(messageId, MSG_RECEIVED);
}
// 2. Then update UI (may fail if message not in UI yet)
ui.updateMessageStatus(messageId, MSG_RECEIVED);

// In main.cpp - onMessageReadReceipt() - Race condition fix
// 1. Update storage FIRST
if (!isSyncing) {
    village.updateMessageStatus(messageId, MSG_READ);
}
// 2. Then update UI
ui.updateMessageStatus(messageId, MSG_READ);
```

---

## Known Working Configuration

### Broker Settings
```cpp
#define MQTT_BROKER "test.mosquitto.org"
#define MQTT_PORT 1883  // Non-TLS

mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
mqttClient.setClientId(clientId.c_str());
mqttClient.setCleanSession(false);
mqttClient.setKeepAlive(60);
// No credentials needed for test.mosquitto.org
```

### Library Version
```ini
# platformio.ini
lib_deps = 
    marvinroger/AsyncMqttClient@^0.9.0
```

---

## Migration Checklist for ESP-MQTT

When migrating to ESP-MQTT, ensure these features are preserved:

- [ ] QoS 1 for all publish operations
- [ ] QoS 1 for all subscriptions
- [ ] Persistent sessions (cleanSession=false)
- [ ] Consistent client ID based on MAC
- [ ] Multi-village support (multiple subscriptions with different keys)
- [ ] Encryption per village (decrypt with correct key based on topic)
- [ ] Auto-ACK on message receipt
- [ ] Status update order (storage first, then UI)
- [ ] Duplicate message detection
- [ ] Sync protocol (request/response)
- [ ] Village name announcements (retained messages)
- [ ] Connection state tracking
- [ ] Automatic reconnection with rate limiting
- [ ] TLS support for HiveMQ Cloud (port 8883)
- [ ] Certificate validation for TLS

---

## Critical Timing and Race Conditions

### Fixed in v0.36.3
1. **ACK arrives before message save completes**
   - Solution: Update storage before UI in onMessageAcked()
   
2. **Message received while not viewing messaging screen**
   - Solution: Always update status to MSG_RECEIVED (status 2) in onMessageReceived()

### Still Existing (but acceptable)
1. **UI update may fail if message not loaded**
   - Not critical - storage is source of truth, UI will sync on next load

---

## Testing Scenarios

### Must Pass After Migration
1. Send message while both devices online → Immediate delivery
2. Send message while recipient offline → Deliver when comes online
3. Multiple villages active → Messages route to correct village
4. Rapid message sending → No duplicates, all ACKs processed
5. Long offline period → Messages queued for days/weeks
6. Device restart → Reconnects with persistent session
7. ACK arrives before message save → Status still updates correctly
8. Message received in main menu → Sender sees delivered status

---

## End of Backup Document
