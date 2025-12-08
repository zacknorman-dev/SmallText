#ifndef MQTT_MESSENGER_H
#define MQTT_MESSENGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <set>
#include <map>
#include "Encryption.h"
#include "Village.h"
#include "LoRaMessenger.h"  // Reuse Message struct and enums

// MQTT Configuration
#define MQTT_BROKER "test.mosquitto.org"  // Public broker for testing
#define MQTT_PORT 1883

// Topic structure: smoltxt/{villageId}/{messageType}
// messageType: shout, whisper/{recipientMAC}, ack/{targetMAC}, read/{targetMAC}

class MQTTMessenger {
private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    Encryption* encryption;
    
    String myVillageId;
    String myVillageName;
    String myUsername;
    uint64_t myMAC;
    String clientId;  // Unique MQTT client ID
    
    // Callbacks (reuse from LoRaMessenger)
    void (*onMessageReceived)(const Message& msg);
    void (*onMessageAcked)(const String& messageId, const String& fromMAC);
    void (*onMessageRead)(const String& messageId, const String& fromMAC);
    void (*onCommandReceived)(const String& command);
    void (*onSyncRequest)(const String& requestorMAC, unsigned long timestamp);  // Sync request from peer
    
    // Connection management
    unsigned long lastReconnectAttempt;
    unsigned long lastPingTime;
    bool connected;
    
    // Duplicate detection
    std::set<String> seenMessageIds;
    unsigned long lastSeenCleanup;
    
    // Sync phase tracking for progressive background sync
    int currentSyncPhase;  // 0 = not syncing, 1 = first 20, 2 = next 20, etc.
    String syncTargetMAC;   // MAC we're syncing with
    unsigned long lastSyncPhaseTime;  // Timestamp of last phase completion
    
    // Helper methods
    String generateMessageId();
    String generateTopic(const String& messageType, const String& target = "");
    void handleIncomingMessage(const String& topic, const uint8_t* payload, unsigned int length);
    void handleSyncRequest(const uint8_t* payload, unsigned int length);
    void handleSyncResponse(const uint8_t* payload, unsigned int length);
    bool reconnect();
    void cleanupSeenMessages();
    
    // Message parsing (similar to LoRa)
    ParsedMessage parseMessage(const String& decrypted);
    
    // Static callback for PubSubClient (must be static)
    static MQTTMessenger* instance;
    static void mqttCallback(char* topic, uint8_t* payload, unsigned int length);

public:
    MQTTMessenger();
    
    bool begin();
    void loop();  // Call frequently to maintain connection and process messages
    
    // Configuration
    void setEncryption(Encryption* enc);
    void setVillageInfo(const String& villageId, const String& villageName, const String& username);
    void setMessageCallback(void (*callback)(const Message& msg));
    void setAckCallback(void (*callback)(const String& messageId, const String& fromMAC));
    void setReadCallback(void (*callback)(const String& messageId, const String& fromMAC));
    void setCommandCallback(void (*callback)(const String& command));
    void setSyncRequestCallback(void (*callback)(const String& requestorMAC, unsigned long timestamp));
    
    // Messaging API (matches LoRaMessenger)
    String sendShout(const String& message);
    String sendWhisper(const String& recipientMAC, const String& message);
    bool sendAck(const String& messageId, const String& targetMAC);
    bool sendReadReceipt(const String& messageId, const String& targetMAC);
    
    // Message sync for offline devices
    bool requestSync(unsigned long lastMessageTimestamp);  // Request messages newer than timestamp
    bool sendSyncResponse(const String& targetMAC, const std::vector<Message>& messages, int phase = 1);  // Send messages to peer (phase 1 = recent 20, phase 2+ = older batches)
    
    // Connection status
    bool isConnected() { return connected && mqttClient.connected(); }
    String getConnectionStatus();
    
    // Sync phase tracking (for UI decisions)
    int getCurrentSyncPhase() const { return currentSyncPhase; }
    
    // MQTT client access for Logger
    PubSubClient* getClient() { return &mqttClient; }
};

#endif
