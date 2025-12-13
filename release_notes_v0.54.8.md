# Release Notes - SmolTxt v0.54.8

## Critical Bug Fixes

### Invite Join Message Loading
**Fixed village initialization after joining via invite code**
- Now properly loads message history before entering messaging screen
- Requests sync from other devices to get existing messages
- Initializes all UI state correctly (inMessagingScreen flag, activity timer)
- Marks unread messages as read and sends read receipts
- **Resolves**: "Save message failed: village not initialized" errors
- **Resolves**: Messages not syncing after invite join
- **Resolves**: Empty message history after successful join

### What Changed
When joining a conversation via invite code in v0.54.7, the app would:
- ❌ Skip loading message history
- ❌ Not request sync from other devices  
- ❌ Leave village data structures uninitialized
- ❌ Cause "village not initialized" errors when sending/receiving

v0.54.8 now properly:
- ✅ Loads existing message history from storage
- ✅ Requests sync to get messages from other devices
- ✅ Initializes all message UI components
- ✅ Sets up proper state flags for messaging screen
- ✅ Enables smooth message sending and receiving

## User Impact

### Before (v0.54.7)
- Messages sent after joining wouldn't save
- Couldn't see any messages in newly joined conversations
- Decryption failures when trying to communicate
- Very fragile, messages not going across devices

### After (v0.54.8)
- Full message history loads immediately after join
- Can send and receive messages right away
- Proper sync with other conversation members
- Stable messaging experience from first message

## Technical Details

Modified `main.cpp` invite join success handler to:
1. Call `village.loadMessages()` to load existing messages
2. Call `mqttMessenger.requestSync()` to sync with other devices
3. Load all messages into UI with `ui.addMessage()`
4. Call `markAndSendReadReceipts()` for proper message status
5. Set `inMessagingScreen = true` and `lastMessagingActivity`
6. Only then transition to `APP_MESSAGING` state

This ensures the village is fully initialized before the messaging screen tries to use it.

---

**Version**: v0.54.8  
**Build Date**: December 12, 2025  
**Previous Version**: v0.54.7
