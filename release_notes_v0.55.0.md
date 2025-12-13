# Release Notes - v0.55.0

**Release Date:** December 13, 2024

## 🚨 CRITICAL BUG FIXES

### Bug #1: Message Attribution Failure (CRITICAL)
**Problem:** Both devices showed ALL messages as coming from the same user, making it impossible to distinguish who sent what.

**Root Cause:** Line 554 in `MQTTMessenger.cpp` was comparing sender **username** instead of **MAC address** to determine message ownership. When both users chose the same username (e.g., "zaq"), the system thought both sent all messages.

**Fix:** Changed comparison from `msg.senderName == msgVillage->username` to `msg.senderMAC == myMacStr` for proper device identification.

**Example of broken behavior:**
```
Device A (MAC: e4cc3bba2010) sends "hello"
Device B (MAC: f8c43bba2010) sees: "[MQTT] Received our own sent message: hello"
```

**After fix:**
```
Device A (MAC: e4cc3bba2010) sends "hello"
Device B (MAC: f8c43bba2010) sees: "[MQTT] Received message from zaq"
```

---

### Bug #2: Sync Encryption Failure (CRITICAL)
**Problem:** Both devices showed `[MQTT] Sync response encryption failed` every time they tried to sync message history.

**Root Cause:** The `sendSyncResponse()` function (line 894) was using `encrypt()` with a 200-byte payload limit, but the JSON sync payload often exceeded this:

```json
{"messages":[{"sender":"zaq","senderMAC":"e4cc3bba2010","content":"test message","timestamp":1765665275,"messageId":"000697d100000002","received":false,"status":1,"villageId":"d142bf04-dec7-4064-a832-7ab37611c802"}],"batch":1,"total":1,"phase":1,"morePhases":false}
```

This JSON is **~260 bytes**, exceeding the 200-byte `MAX_PLAINTEXT` limit.

**Fix:** 
1. Increased `MAX_PLAINTEXT` from 200 to 512 bytes in `Encryption.h`
2. Increased sync buffer from 512 to 600 bytes in `MQTTMessenger.cpp` (to accommodate ciphertext overhead: 512 + 12 nonce + 16 tag = 540 bytes)

**Impact:** Message sync now works correctly between devices. Historical messages are properly exchanged.

---

## 📊 What Was NOT a Bug

### "Old messages appearing" / "wrongUUID=345"
**User Report:** Logs showed `File stats: total=613 empty=266 parseErr=0 wrongUUID=345 matched=2`

**Clarification:** This is **correct behavior**, not a bug. The system:
- Has 613 total lines in `messages.dat` from all conversations ever created
- Filters messages by village UUID when loading
- Found 345 lines belonging to other villages (correctly rejected)
- Found 2 lines matching the current conversation (correctly loaded)

The filtering is working **exactly as designed** to prevent cross-conversation data leakage.

---

## 🔧 Technical Changes

### Files Modified:
1. **src/MQTTMessenger.cpp** (Line 554-566)
   - Changed message attribution logic from username comparison to MAC address comparison
   - Ensures devices correctly identify their own messages vs. received messages

2. **src/Encryption.h** (Line 9)
   - Increased `MAX_PLAINTEXT` from 200 to 512 bytes
   - Allows larger JSON payloads for message sync

3. **src/MQTTMessenger.cpp** (Line 894)
   - Increased sync encryption buffer from 512 to 600 bytes
   - Matches new ciphertext size requirements

4. **src/version.h**
   - Updated version to v0.55.0

---

## ✅ Testing Recommendations

### Test Case 1: Message Attribution
1. Create new conversation with both devices
2. Both users choose the **same username** (e.g., "zaq")
3. Send messages from both devices
4. **Verify:** Each device shows received messages with correct sender indication

### Test Case 2: Message Sync
1. Turn off one device
2. Send 3-5 messages from the other device
3. Turn on the offline device
4. **Verify:** All messages sync successfully without "encryption failed" errors
5. **Verify:** Message history is identical on both devices

### Test Case 3: Multi-Conversation Isolation
1. Create conversation "A" with device pair 1+2
2. Send messages in conversation "A"
3. Create conversation "B" with device pair 1+3
4. **Verify:** Messages from "A" never appear in "B"
5. Check logs for proper `wrongUUID` filtering

---

## 🐛 Known Issues / User Experience Notes

### Same Username Confusion
If both users in a conversation choose the **same username**, the UI will show:
```
zaq: hello
zaq: how are you?
zaq: good thanks
```

**Workaround:** Choose different usernames or refer to message status indicators:
- Messages you sent: Show delivery status (✓ sent, ✓✓ received, ✓✓ read)
- Messages you received: Show timestamp

**Future Enhancement:** Add "(you)" suffix or use different text styling for own messages.

---

## 🔄 Upgrade Path

### From v0.54.15 → v0.55.0:
1. Both devices should update simultaneously
2. After update, create a **new conversation** (don't reuse old ones)
3. Old conversations may have corrupted sync data from v0.54.15 bugs
4. Consider deleting conversations created with v0.54.11-v0.54.15

### Fresh Start Recommended:
Due to the severity of the message attribution bug, conversations created with v0.54.11-v0.54.15 may have incorrect sender data in `messages.dat`. Starting fresh ensures clean state.

---

## 📈 Impact Summary

**Severity:** CRITICAL  
**Scope:** All message sending/receiving and sync operations  
**Affected Versions:** v0.54.11 through v0.54.15  
**Status:** ✅ RESOLVED in v0.55.0

**Recommendation:** All users should upgrade to v0.55.0 immediately and create new conversations for testing.
