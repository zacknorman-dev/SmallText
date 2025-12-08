# SmolTxt v0.22.0 - MQTT Debug Logging

## 🆕 New Features

### Remote Log Monitoring
- **MQTT Debug Logging**: All device logs now stream over MQTT to topic `smoltxt/{deviceMAC}/debug`
- **View logs from anywhere**: No USB cable required - monitor devices remotely via WiFi
- **Multi-device monitoring**: Use wildcard subscriptions to view logs from all devices simultaneously

## 📡 Usage

### View logs from a single device:
```bash
mosquitto_sub -h test.mosquitto.org -t "smoltxt/a1b2c3d4e5f6/debug"
```

### View logs from ALL devices:
```bash
mosquitto_sub -h test.mosquitto.org -t "smoltxt/+/debug"
```

## 🔧 Technical Changes

- Added `PubSubClient` integration to Logger class
- Logger automatically publishes all log messages to MQTT when connected
- Log format: `[timestamp] LogLevel: message`
- Topic format: `smoltxt/{deviceMAC}/debug` (unique per device)
- Added `getClient()` method to MQTTMessenger for Logger access

## 🎯 Benefits

- **Remote Debugging**: Troubleshoot devices without physical access
- **Fleet Monitoring**: Watch multiple devices simultaneously
- **Field Support**: Get real-time logs from deployed devices
- **Historical Records**: MQTT broker can retain logs for later analysis

## 📦 Installation

Flash this firmware via OTA or USB. Devices will automatically start streaming logs once connected to WiFi and MQTT.

## 🔄 Changelog

**v0.22.0**
- Added MQTT debug logging for remote monitoring
- Logs stream to `smoltxt/{deviceMAC}/debug` topic
- No configuration required - works automatically after WiFi connection

**v0.21.0** (Previous)
- Fixed LoadProhibited crashes when entering villages
- Removed all LoRa messenger calls causing null pointer errors
