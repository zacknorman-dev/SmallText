# Release Notes - v0.54.12

**Release Date:** December 12, 2025  
**Critical Bug Fix Release**

## 🔴 Critical Fixes

### Encryption Key Format Bug (BLOCKING ISSUE)
**Problem:** Villages joined via invite could not decrypt any messages - complete communication failure.

**Root Cause:** Triple field mismatch when joining via invite:
1. **Key format mismatch:** Invite join saved encryption key as base64 (44 chars), but `Village::loadFromSlot()` expected hex string (64 chars)
2. **Key field name:** Saved as `"encryptionKey"` but loader expected `"key"`
3. **Username field name:** Saved as `"myUsername"` but loader expected `"username"`

**Impact:** 
- Joiner could not decrypt creator's messages ❌
- Creator could not decrypt joiner's messages ❌
- Sync requests failed with decrypt errors ❌
- Read receipts failed with decrypt errors ❌

**Fix:**
```cpp
// BEFORE (BROKEN):
doc["encryptionKey"] = base64EncodedKey;  // Wrong format AND wrong field name
doc["myUsername"] = "member";              // Wrong field name

// AFTER (FIXED):
String keyHex = "";
for (int i = 0; i < 32; i++) {
  sprintf(hex, "%02x", pendingInvite.encryptionKey[i]);
  keyHex += hex;
}
doc["key"] = keyHex;        // Correct: 64-char hex string
doc["username"] = "member";  // Correct field name
```

**Evidence from Logs:**
```
[184749] INFO: Invite received: jetblue
[188761] INFO: Joined village: jetblue
[95850] ERROR: MQTT: Decryption failed for jetblue  ← ALL MESSAGES FAILING
[98458] ERROR: MQTT: Decryption failed for jetblue
[99664] ERROR: Sync request decrypt failed
```

### Conversation List Corruption Bug
**Problem:** After deleting a village with backspace, re-entering conversation list showed OLD deleted villages (ghost entries).

**Root Cause:** `conversationList` vector not cleared after deletion, so stale data persisted in memory.

**Fix:**
```cpp
// Delete the village slot
Village::deleteSlot(entry.slot);

// ADDED: Clear stale conversation list
conversationList.clear();  // ← Forces rebuild on next view

// Return to main menu
appState = APP_MAIN_MENU;
```

**Evidence from User Report:**
> "when I hit backspace it turns the whole menu into an old version of the menu that references villages I [already deleted]"

### Username Pre-fill Bug
**Problem:** When entering messaging screen after joining via invite, username text appeared pre-filled in message input box.

**Fix:** Added aggressive input clearing:
```cpp
// BEFORE:
keyboard.clearInput();
ui.setInputText("");

// AFTER:
keyboard.clearInput();
keyboard.update();          // ← Process the clear immediately
ui.setInputText("");
ui.update();                // ← Force UI to process the clear
smartDelay(50);             // ← Ensure clearing completes
```

## Testing Validation

**Test Scenario:**
1. Device A (COM14): Create village "jetblue" with v0.54.12
2. Device A: Publish invite code
3. Device B (COM13): Join via invite code
4. Both devices: Send messages back and forth

**Expected Results (v0.54.12):**
- ✅ Encryption key saved as 64-char hex: `"key": "a1b2c3..."`
- ✅ All fields match loader expectations: `"key"`, `"username"`, `"villageName"`
- ✅ Messages decrypt successfully on both sides
- ✅ Sync requests decrypt successfully
- ✅ Read receipts work properly
- ✅ Village deletion clears menu state
- ✅ No pre-filled text in message input

**Comparison to v0.54.11 (BROKEN):**
- ❌ Key saved as base64: `"encryptionKey": "obK8w..."`
- ❌ Field mismatch: loader couldn't find encryption key
- ❌ All decryption failed: "Decryption failed for jetblue"
- ❌ Communication completely broken

## Technical Details

### Key Format Specifications
- **Storage format:** 64-character lowercase hex string (e.g., `"a1b2c3d4e5..."`)
- **Transmission format:** Base64 over TLS (e.g., `"obK8w3TlZm..."`)
- **In-memory format:** 32-byte array (`uint8_t[32]`)

### Field Name Standards
| Data | Field Name | Type | Notes |
|------|------------|------|-------|
| Encryption key | `"key"` | String (hex) | 64 chars |
| Village ID | `"villageId"` | String (UUID) | 36 chars |
| Village name | `"villageName"` | String | User-defined |
| Username | `"username"` | String | User-defined |

### Files Modified
1. **src/main.cpp** (3 fixes):
   - Line ~2376: Changed encryption key to hex format, fixed field names
   - Line ~1388: Added `conversationList.clear()` after village deletion
   - Line ~2481: Added aggressive input clearing before messaging

2. **src/version.h**:
   - Updated `BUILD_NUMBER` to `"v0.54.12"`

## Migration Notes

**Existing Villages:**
- Villages created with v0.54.11 will NOT work with v0.54.12 (incompatible key format)
- **Action required:** Delete all villages created with v0.54.11 using backspace delete
- Fresh village creation/join required after update

**Update Path:**
- v0.54.11 → v0.54.12: OTA update available
- After update: Use backspace to delete old villages, create new ones

## Architecture Notes

This bug revealed the critical importance of **format consistency** between save and load operations:

**Correct Pattern:**
```cpp
// Save (Village::saveToSlot)
String keyHex = "";
for (int i = 0; i < KEY_SIZE; i++) {
  sprintf(hex, "%02x", encryptionKey[i]);
  keyHex += hex;
}
doc["key"] = keyHex;

// Load (Village::loadFromSlot)
String keyHex = doc["key"].as<String>();
for (int i = 0; i < KEY_SIZE; i++) {
  char byte[3] = {keyHex[i*2], keyHex[i*2+1], '\0'};
  encryptionKey[i] = strtol(byte, NULL, 16);
}
```

**Broken Pattern (v0.54.11):**
```cpp
// Save (main.cpp invite join)
mbedtls_base64_encode(..., pendingInvite.encryptionKey, 32);
doc["encryptionKey"] = base64String;  // ← Wrong format + wrong field

// Load (Village::loadFromSlot)
String keyHex = doc["key"].as<String>();  // ← Can't find key!
// Decryption uses garbage data → ALL MESSAGES FAIL
```

## Previous Issues Resolved

This release builds on v0.54.11's architecture improvements:
- ✅ Pure random encryption keys (no password derivation)
- ✅ Pure random UUID generation (no deterministic IDs)
- ✅ Invite-based key transmission via TLS

**v0.54.12 completes the implementation** by fixing the critical save/load format bug that broke the entire system.

---

**Status:** Ready for testing  
**Priority:** CRITICAL - Blocks all invite-based communication  
**Deployment:** Immediate OTA update recommended
