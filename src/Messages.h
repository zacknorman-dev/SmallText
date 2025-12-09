#ifndef MESSAGES_H
#define MESSAGES_H

#include <Arduino.h>

// Message types
enum MessageType {
    MSG_UNKNOWN = 0,
    MSG_SHOUT = 1,
    MSG_WHISPER = 2,
    MSG_ACK = 3,
    MSG_READ_RECEIPT = 4,
    MSG_SYNC_REQUEST = 5,
    MSG_SYNC_RESPONSE = 6,
    MSG_COMMAND = 7,
    MSG_VILLAGE_NAME_REQUEST = 8,
    MSG_VILLAGE_NAME_RESPONSE = 9
};

// Message status (for UI indication)
enum MessageStatus {
    MSG_PENDING = 0,
    MSG_SENT = 1,
    MSG_RECEIVED = 2,
    MSG_READ = 3,
    MSG_SEEN = 4  // Alias for MSG_READ for backwards compatibility
};

// Message structure
struct Message {
    String sender;
    String senderMAC;
    String content;
    unsigned long timestamp;
    bool received;
    MessageStatus status;
    String messageId;
};

// Parsed message structure (for MQTT protocol parsing)
struct ParsedMessage {
    MessageType type = MSG_UNKNOWN;
    String villageId;
    String target;
    String senderName;
    String senderMAC;
    String messageId;
    String content;
    int currentHop = 0;
    int maxHop = 0;
};

#endif
