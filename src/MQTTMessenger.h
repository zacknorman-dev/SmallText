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
    
    // Connection management
    unsigned long lastReconnectAttempt;
    unsigned long lastPingTime;
    bool connected;
    
    // Duplicate detection
    std::set<String> seenMessageIds;
    unsigned long lastSeenCleanup;
    
    // Helper methods
    String generateMessageId();
    String generateTopic(const String& messageType, const String& target = "");
    void handleIncomingMessage(const String& topic, const uint8_t* payload, unsigned int length);
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
    
    // Messaging API (matches LoRaMessenger)
    bool sendShout(const String& message);
    bool sendWhisper(const String& recipientMAC, const String& message);
    bool sendAck(const String& messageId, const String& targetMAC);
    bool sendReadReceipt(const String& messageId, const String& targetMAC);
    
    // Connection status
    bool isConnected() { return connected && mqttClient.connected(); }
    String getConnectionStatus();
};

#endif
