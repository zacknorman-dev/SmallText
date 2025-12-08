**Critical Crash Fix - MQTT-Only Mode**

This release fixes crashes that occurred when entering villages and sending messages.

**Bug Fix:**
- ✅ **Fixed LoadProhibited crash** - Removed all LoRa messenger calls that were causing null pointer crashes
- ✅ **Stable MQTT messaging** - Villages and messaging now work reliably

**Changes from v0.20.0:**
- Commented out all `messenger.setEncryption()` calls
- Commented out all `messenger.setVillageInfo()` calls  
- Commented out all `messenger.sendMessage()` calls
- Fixed crash on village entry and message sending

**Technical Details:**
- BUILD_NUMBER: v0.21.0
- FIRMWARE_VERSION: 0.21.0
- Messaging: MQTT-only via test.mosquitto.org:1883
- LoRa: Completely disabled (no crashes from null pointers)
- OTA endpoint: https://raw.githubusercontent.com/zacknorman-dev/SmallText/refs/heads/main/firmware.bin
