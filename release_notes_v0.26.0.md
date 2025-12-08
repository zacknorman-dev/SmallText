# v0.26.0 - Debug Message Sending

## 🔍 Debugging Changes

### Enhanced Logging for Message Sending
- Added detailed logs to track MQTT connection status when sending
- Logs now show if messages fail to send and why
- Added message save operation for sent messages

### What to Look For in Logs
When you send a message, you'll now see:
```
INFO: Attempting MQTT send, connected: 1
INFO: MQTT send SUCCESS, ID: abc123
```

Or if it fails:
```
INFO: Attempting MQTT send, connected: 0
ERROR: MQTT not connected - message not sent
```

## 🎯 Purpose
This version adds diagnostic logging to help identify why messages aren't being sent over MQTT. Watch the debug logs after updating to see what's happening when you send messages.

## 📡 Remote Update Note
The remote `update` command only works when user is on **main menu or village menu**. If user is in messaging or typing, the command is ignored with log:
```
INFO: OTA: Update command ignored - user is busy
```

Wait until they're on a menu screen, then send the command again.
