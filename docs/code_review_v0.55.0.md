# Code Review: Sender Attribution and Message Flow

## Overview
This document explains the "spaghetti code" issues discovered in v0.54.15 that caused message attribution failures and user confusion.

---

## The Core Problem: Inconsistent Message Identification

### Three Ways to Identify Messages
The codebase uses **three different attributes** to identify message ownership:

1. **`msg.senderName`** - Username string (e.g., "zaq", "alice", "bob")
2. **`msg.senderMAC`** - Device MAC address (e.g., "e4cc3bba2010", "f8c43bba2010")
3. **`msg.received`** - Boolean flag (true = received from others, false = we sent it)

### Why This Causes Confusion

```cpp
// OLD BROKEN CODE (v0.54.15 - Line 554)
if (msgVillage && msg.senderName == msgVillage->username) {
    m.received = false;  // "This is OUR message"
} else {
    m.received = true;   // "This is someone else's message"
}
```

**Scenario:** Both users choose username "zaq"
- Device A (MAC: e4cc3bba2010, username: "zaq") sends "hello"
- Device B (MAC: f8c43bba2010, username: "zaq") receives the message
- Device B compares: `"zaq" == "zaq"` → TRUE → thinks it sent the message!

**Result:** Both devices think they sent ALL messages.

---

## Message Flow Architecture Analysis

### 1. Message Send Path
```
User types message
  ↓
main.cpp::handleMessaging() [Line 2750]
  ↓
MQTTMessenger::sendShout(message, username) [Line 620]
  ↓
Format: "SHOUT:villageId:*:username:macAddr:msgId:content:0:0"
  ↓
Encrypt with village key
  ↓
Publish to MQTT topic: "smoltxt/{villageId}/shout"
  ↓
MQTT broker broadcasts to ALL subscribers
  ↓
BOTH devices receive (including sender)
```

**Key Insight:** The sender **also receives their own message** via MQTT broadcast (QoS 1 retained delivery).

---

### 2. Message Receive Path
```
MQTT message arrives
  ↓
MQTTMessenger::handleMqttEvent() [Line 145]
  ↓
Decrypt message
  ↓
Parse: "SHOUT:villageId:*:username:macAddr:msgId:content:0:0"
  ↓
MQTTMessenger::processMessage() [Line 507]
  ↓
Check if senderMAC == myMAC
  - YES → Skip (line 534: "Ignoring our own message")
  - NO → Continue processing
  ↓
Determine if received or sent (LINE 554 - THE BUG!)
  ↓
Call onMessageReceived() callback
  ↓
main.cpp::onMessageReceived() [Line 438]
  ↓
Save to messages.dat
  ↓
Update UI
```

---

## The Bug in Detail

### Before Fix (v0.54.15)
```cpp
// Line 554 - WRONG: Compares USERNAME
if (msgVillage && msg.senderName == msgVillage->username) {
    m.received = false;  // Mark as "our message"
}
```

**Problem:** When both users have username "zaq":
- Device A's `msgVillage->username` = "zaq"
- Device B's `msgVillage->username` = "zaq"
- ALL messages from "zaq" are treated as "our message" on BOTH devices

### After Fix (v0.55.0)
```cpp
// Line 554 - CORRECT: Compares MAC ADDRESS
if (msg.senderMAC == myMacStr) {
    m.received = false;  // Mark as "our message"
}
```

**Solution:** MAC addresses are globally unique:
- Device A's `myMacStr` = "e4cc3bba2010"
- Device B's `myMacStr` = "f8c43bba2010"
- Only messages from the EXACT device MAC are marked as "our message"

---

## System Message Confusion

### "SmolTxt" vs "system" Sender
The codebase has **two types** of system messages:

1. **System Messages from App** (e.g., "zaq joined the conversation")
   - Sender name: `"SmolTxt"`
   - Sender MAC: `"system"`
   - Format: `SHOUT:villageId:*:SmolTxt:system:msgId:content:0:0`

2. **Regular User Messages**
   - Sender name: `"zaq"` (username)
   - Sender MAC: `"e4cc3bba2010"` (device MAC)
   - Format: `SHOUT:villageId:*:zaq:e4cc3bba2010:msgId:content:0:0`

### Why This Is Confusing

```cpp
// Line 534 - Check if message is from us
if (msg.senderMAC == myMacStr) {
    Serial.println("[MQTT] Ignoring our own message");
    return;
}
```

**Q:** What happens when device receives a system message?
**A:** System messages have `senderMAC = "system"`, which never matches any device MAC, so they're always processed.

**Q:** Can a device send a system message and receive it?
**A:** Yes! When a device sends `sendSystemMessage("zaq joined")`, it broadcasts to MQTT and receives its own system message.

**Q:** How does it know not to process its own system message?
**A:** Line 554 check: `if (msg.senderMAC == myMacStr)` → `"system" != "e4cc3bba2010"` → FALSE → processes it!

**Result:** System messages are **intentionally** processed by the sender device. This is correct behavior for join/leave announcements.

---

## Message Status State Machine

Messages have a **status field** that tracks delivery:

```
MSG_SENT (1)        → Initial state when you send a message
  ↓ (receive ACK)
MSG_RECEIVED (2)    → Other device acknowledged receiving
  ↓ (receive READ_RECEIPT)
MSG_READ (3)        → Other device opened messaging screen
```

### The Issue: Status Reset on Sync

**Scenario:**
1. Device A sends "hello" → status = MSG_SENT (1)
2. Device B receives "hello" → status = MSG_RECEIVED (2) on device B
3. Device A syncs with Device B
4. Sync response contains message with `status = 2`
5. Device A receives sync → **overwrites local status from 1 to 2**

**Problem:** The sync process can **downgrade** or **upgrade** status incorrectly.

**Solution (not in this version):** Sync should use `max(local_status, remote_status)` to preserve highest state.

---

## Encryption Key Confusion

### Multi-Village Problem
Each conversation has its own encryption key, but the sync system was using the **current village's key** instead of the **target village's key**.

```cpp
// OLD BROKEN CODE (v0.54.14)
bool MQTTMessenger::sendSyncResponse(...) {
    // Uses this->encryption (current village key)
    encryption->encryptString(payload, encrypted, ...);
}
```

**Problem:** When syncing messages for village A while village B is active, encryption uses wrong key.

### Fixed in v0.54.15
```cpp
// v0.54.15 - Line 876
VillageSubscription* village = findVillageSubscription(villageId);
Encryption villageEncryption;
villageEncryption.setKey(village->encryptionKey);
// Now uses village-specific key
villageEncryption.encrypt(...);
```

**But:** This introduced Bug #2 (payload size issue), which is fixed in v0.55.0 by increasing buffer sizes.

---

## Recommendations for Code Cleanup

### 1. Single Source of Truth for Message Ownership
**Current:** `msg.received` boolean is set based on username comparison  
**Better:** Always derive from MAC address comparison on-the-fly

```cpp
// Proposed helper function
bool Message::isOwnMessage(const String& deviceMAC) const {
    return this->senderMAC == deviceMAC;
}
```

### 2. Separate System Messages from User Messages
**Current:** System messages use `senderMAC = "system"` and `senderName = "SmolTxt"`  
**Better:** Use a dedicated message type enum

```cpp
enum MessageType {
    MSG_TYPE_USER,      // Regular user message
    MSG_TYPE_SYSTEM,    // System announcements (join/leave)
    MSG_TYPE_WHISPER    // Private message
};
```

### 3. Explicit Message Status Rules
**Current:** Status is set/updated in multiple places inconsistently  
**Better:** Centralize status transitions with validation

```cpp
class Message {
    bool transitionStatus(MessageStatus newStatus) {
        // Validate transition (e.g., can't go from READ back to SENT)
        if (newStatus < this->status) {
            return false;  // Invalid downgrade
        }
        this->status = newStatus;
        return true;
    }
};
```

### 4. Decouple MQTT from Village Logic
**Current:** MQTTMessenger knows about villages, encryption, usernames  
**Better:** Use dependency injection and interfaces

```cpp
class IEncryptionProvider {
    virtual uint8_t* getKeyForVillage(String villageId) = 0;
};

class MQTTMessenger {
    IEncryptionProvider* encryptionProvider;
    
    void sendMessage(String villageId, String content) {
        uint8_t* key = encryptionProvider->getKeyForVillage(villageId);
        // ... encrypt and send
    }
};
```

### 5. Add Unit Tests for Message Attribution
```cpp
TEST(MessageAttribution, DifferentUsers) {
    Message msg;
    msg.senderMAC = "aaaaaaaaaaaa";
    ASSERT_FALSE(msg.isOwnMessage("bbbbbbbbbbbb"));
}

TEST(MessageAttribution, SameUsername) {
    Message msg1, msg2;
    msg1.senderName = "zaq";
    msg1.senderMAC = "device1";
    msg2.senderName = "zaq";
    msg2.senderMAC = "device2";
    
    ASSERT_FALSE(msg1.isOwnMessage("device2"));
    ASSERT_TRUE(msg1.isOwnMessage("device1"));
}
```

---

## Conclusion

The "spaghetti code" issue stems from:
1. **Inconsistent identification** (username vs MAC vs received flag)
2. **Implicit assumptions** (usernames are unique - WRONG!)
3. **Lack of clear boundaries** (MQTT layer knows too much about villages)
4. **State management scattered** across multiple files without centralized rules

**Short-term fix (v0.55.0):** Use MAC address for attribution  
**Long-term fix:** Architectural refactoring to separate concerns and establish clear contracts
