# v0.33.9 - Read Receipt Fix

## Fixed Read Receipts Not Being Sent

**The Problem:**
- Read receipts were never being queued or sent when viewing messages
- Condition checked `msg.status != MSG_RECEIVED` to determine if message was new
- But all incoming MQTT messages have `MSG_RECEIVED` status by default
- So the condition always failed and read receipts were never queued

**The Solution:**
- Removed the incorrect status check from the condition
- Now read receipts queue whenever you're viewing the messaging screen
- Background task sends them via MQTT to the sender
- Messages will now properly show as READ on the sender's device

## Changes
- Simplified `onMessageReceived` condition (removed `msg.status != MSG_RECEIVED` check)
- Read receipts now work correctly for all incoming messages
- Messages show as "READ" (status 2) after being viewed by recipient

## Testing
Flash v0.33.9 and verify:
- Send a message from device A
- View it on device B (open messaging screen)
- Check device A - message should update to READ status
- Check logs for `[App] Sending queued read receipt for: ...`
- Check logs for `[MQTT] Received read receipt for message: ...`
