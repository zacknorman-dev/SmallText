#ifndef LORA_MESSENGER_H
#define LORA_MESSENGER_H

#include <Arduino.h>
#include <RadioLib.h>
#include <set>
#include <map>
#include "Encryption.h"
#include "Village.h"

// New message protocol format:
// "TYPE:villageName:target:senderMAC:msgId:content:hop:maxHop"
// TYPE = SHOUT (broadcast) | GROUP (group) | WHISPER (direct) | ACK (acknowledgment)

#define MAX_LORA_PAYLOAD 255
#define MAX_MESSAGE_CHARS 150

enum MessageType {
    MSG_SHOUT,    // Broadcast to all village members
    MSG_GROUP,    // Group message (filtered by group name)
    MSG_WHISPER,  // Direct 1-on-1 message
    MSG_ACK,      // Acknowledgment of receipt
    MSG_READ_RECEIPT,  // Read receipt
    MSG_VILLAGE_NAME_REQUEST,  // Joiner requests village name
    MSG_VILLAGE_ANNOUNCE,  // Village name announcement (creator -> joiner)
    MSG_UNKNOWN
};

struct ParsedMessage {
    MessageType type;
    String villageId;     // UUID for filtering
    String villageName;   // Display name (not used for filtering)
    String target;        // "*" for SHOUT, groupName for GROUP, recipientMAC for WHISPER
    String senderName;    // Display name of sender
    String senderMAC;
    String messageId;
    String content;
    int currentHop;
    int maxHop;
    
    ParsedMessage() : type(MSG_UNKNOWN), currentHop(0), maxHop(0) {}
};

enum MessageStatus {
    MSG_SENT,      // Transmitted successfully
    MSG_RECEIVED,  // At least one device ACKed
    MSG_SEEN,      // Some recipients opened chat (groups only)
    MSG_READ       // All recipients have seen it
};

struct Message {
    String sender;
    String senderMAC;  // MAC address of sender (for sending receipts)
    String content;
    unsigned long timestamp;
    bool received;  // true if received, false if sent
    MessageStatus status;  // Delivery/read status
    String messageId;  // Unique ID for tracking ACKs
    std::set<String> ackedBy;  // MACs of devices that ACKed
    std::set<String> readBy;   // MACs of devices that read it
};

class LoRaMessenger {
private:
    SX1262* radio;
    Encryption* encryption;
    String myVillageId;    // UUID for filtering
    String myVillageName;  // Human-readable name for display
    String myUsername;
    uint64_t myMAC;
    
    void (*onMessageReceived)(const Message& msg);
    void (*onMessageAcked)(const String& messageId, const String& fromMAC);
    void (*onMessageRead)(const String& messageId, const String& fromMAC);
    void (*onVillageNameReceived)(const String& villageName);  // Callback for village name announcement
    void (*delayCallback)(unsigned long ms);  // Callback for delays to keep keyboard responsive
    
    String lastSentMessageId;  // Store ID of last sent message
    
    static volatile bool receivedFlag;  // Set by interrupt
    static void setFlag(void);  // Interrupt callback
    
    // Mesh networking
    std::set<String> seenMessageIds;  // Duplicate detection
    unsigned long lastSeenCleanup;
    
    // Echo detection - track recently transmitted packets
    std::map<uint32_t, unsigned long> recentTransmissions;  // hash -> timestamp
    unsigned long lastTransmissionCleanup;
    
    // Message parsing and formatting
    String generateMessageId();
    String formatMessage(MessageType type, const String& target, const String& content, int maxHop);
    ParsedMessage parseMessage(const String& decrypted);
    bool isGarbage(const String& text);
    
    // Forwarding logic
    void handleReceivedMessage(const ParsedMessage& msg);
    bool shouldForward(const ParsedMessage& msg);
    void forwardMessage(const String& encrypted, const ParsedMessage& msg);
    
    uint32_t generateVillageId(const String& villageName);
    uint32_t hashPacket(const uint8_t* data, size_t len);
    
public:
    LoRaMessenger();
    
    bool begin(int csPin, int dio1Pin, int resetPin, int busyPin);
    void setEncryption(Encryption* enc);
    void setVillageInfo(const String& villageId, const String& villageName, const String& username);
    void setMessageCallback(void (*callback)(const Message& msg));
    void setAckCallback(void (*callback)(const String& messageId, const String& fromMAC));
    void setReadCallback(void (*callback)(const String& messageId, const String& fromMAC));
    void setVillageNameCallback(void (*callback)(const String& villageName));  // Set callback for village name
    void setDelayCallback(void (*callback)(unsigned long ms));  // Set callback for responsive delays
    
    // New messaging API
    bool sendShout(const String& message);  // Broadcast to all
    bool sendGroup(const String& groupName, const String& message);  // Group message
    bool sendWhisper(const String& recipientMAC, const String& message);  // Direct message
    bool sendAck(const String& messageId, const String& targetMAC);  // Send ACK
    bool sendReadReceipt(const String& messageId, const String& targetMAC);  // Send read receipt
    bool sendVillageNameRequest();  // Request village name from owner
    bool sendVillageNameAnnouncement();  // Broadcast village name to members
    
    void loop();  // Process messages and handle mesh forwarding
    
    String getLastSentMessageId() const { return lastSentMessageId; }
    static void clearReceivedFlag() { receivedFlag = false; }  // Explicitly clear flag
    
    // Legacy methods
    bool sendMessage(const String& message);
    void checkForMessages();  // Call this in loop()
    
    // Radio testing (unencrypted)
    bool sendRaw(const String& message);
    String checkForMessage();  // Returns received message or empty string
    
    // Configuration
    void setFrequency(float freq);
    void setBandwidth(float bw);
    void setSpreadingFactor(uint8_t sf);
    void setOutputPower(int8_t power);
};

#endif
