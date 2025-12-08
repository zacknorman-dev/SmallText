# v0.24.0 - Message ID Fix

## 🐛 Critical Bug Fix

### Messages Now Save With Proper IDs
- Fixed issue where sent messages had empty IDs (`id=`)
- Message IDs are now properly captured and saved locally when sending
- This fixes message tracking and status updates

**What Was Wrong:**
When you sent a message, the MQTT layer generated an ID but the local copy saved to disk had no ID. This prevented proper message tracking and ACK/read receipt matching.

**What's Fixed:**
- `sendShout()` and `sendWhisper()` now return the message ID
- Local message object gets the same ID as the MQTT message
- Messages can now be properly tracked throughout their lifecycle

## 🔄 OTA Update

Devices will automatically download and apply this update. Messages should now send and receive properly with full ID tracking!
