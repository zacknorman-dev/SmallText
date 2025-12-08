# v0.23.0 - Remote Commands & Message Fix

## 🐛 Bug Fixes

### Messages Now Send Properly
- Fixed village info not being set to MQTT messenger during startup
- Village configuration now set regardless of MQTT connection status
- Messages will now send correctly when you enter a village

## 🎮 New Features

### Remote Device Commands via MQTT
You can now send commands to your devices remotely over MQTT!

**Supported Commands:**
- `update` - Forces device to check for OTA updates immediately
- `reboot` - Reboots the device

**How to Use:**

Get your device's MAC address from debug logs, then send a command:
```bash
mosquitto_pub -h test.mosquitto.org -t "smoltxt/a1b2c3d4e5f6/command" -m "update"
mosquitto_pub -h test.mosquitto.org -t "smoltxt/a1b2c3d4e5f6/command" -m "reboot"
```

## 🔧 Technical Changes

- **MQTTMessenger**: Added command callback system and `/command` topic subscription
- **main.cpp**: Implemented `onCommandReceived()` handler for remote commands
- **Bug Fix**: Removed connection checks that prevented village info from being set during boot

## 🔄 OTA Update

Devices will automatically download and apply this update. You can also force an update by sending:
```bash
mosquitto_pub -h test.mosquitto.org -t "smoltxt/YOUR_DEVICE_MAC/command" -m "update"
```
