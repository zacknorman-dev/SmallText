# Release Notes - v0.36.2

## Major Infrastructure Changes

### MQTT Broker Migration
- **Switched from test.mosquitto.org to HiveMQ Cloud**
  - Production-grade broker with reliable persistent session support
  - TLS/SSL encryption on port 8883
  - Free tier: 100 connections, 10GB/month data transfer

### Offline Message Queuing Implementation
- **Migrated from PubSubClient to AsyncMqttClient library**
  - Full QoS 1 support for guaranteed message delivery
  - Persistent sessions (clean session = false) for offline message queuing
  - Messages sent while peer is offline are automatically queued by broker
  - Messages delivered automatically when peer reconnects (hours/days/weeks later)

### Message Delivery Improvements
- **All MQTT publish operations now use QoS 1**
  - Shout messages: QoS 1 with at-least-once delivery guarantee
  - Whisper messages: QoS 1 for private messages
  - Acknowledgments: QoS 1
  - Read receipts: QoS 1
  - Sync requests/responses: QoS 1
  - Village name announcements: QoS 1 with retained flag

### Connection Stability
- **Persistent client IDs based on device MAC address**
  - Ensures broker recognizes reconnecting clients
  - Enables proper session restoration after disconnect/reboot
- **Asynchronous MQTT operations**
  - Non-blocking connection handling
  - Better WiFi integration
  - Callbacks for connect/disconnect/message events

## Technical Details

### AsyncMqttClient Implementation
- Static callback handlers for thread-safe operation
- `onMqttConnect()`: Handles connection and re-subscription
- `onMqttDisconnect()`: Connection loss tracking
- `onMqttMessage()`: Message routing and processing

### Subscription Management
- All subscriptions use QoS 1 for reliable message delivery
- Village topics: `smoltxt/{villageId}/#`
- Command topic: `smoltxt/{deviceMAC}/command`
- Sync response topic: `smoltxt/{deviceMAC}/sync-response`

### Removed Dependencies
- Removed PubSubClient library (QoS 0 only)
- Removed MQTT logging from Logger class (simplified)

## Testing Verified

### Command-Line Testing
- Persistent session message queuing verified with mosquitto_sub/mosquitto_pub
- QoS 1 message delivery confirmed while subscriber offline
- Session restoration tested with client reconnection
- HiveMQ Cloud broker properly queues messages during offline periods

### Expected Behavior
1. Device A sends message to Device B (Device B offline)
2. HiveMQ Cloud queues message with QoS 1
3. Device B comes online (minutes/hours/days later)
4. Device B automatically receives queued message on reconnection
5. No manual sync required - truly asynchronous messaging

## Breaking Changes
- **MQTT broker changed** - devices must be reflashed with v0.36.2
- All devices in a village must use same broker (HiveMQ Cloud)
- Credentials embedded in firmware (smoltok / QdgMc7VnQ2D8dhT)

## Known Issues
- First deployment to HiveMQ Cloud - monitoring for stability
- Free tier has 100 connection limit (sufficient for current user base)
- TLS connection requires proper certificate handling on ESP32

## Future Considerations
- Monitor HiveMQ Cloud usage and performance
- Consider credential management for multi-user deployments
- Evaluate session expiry settings (currently using broker defaults)
- May need upgrade to paid tier if usage exceeds free limits

## Files Changed
- `src/MQTTMessenger.h` - AsyncMqttClient integration
- `src/MQTTMessenger.cpp` - Complete rewrite for QoS 1 support
- `src/Logger.h` - Removed PubSubClient dependency
- `src/Logger.cpp` - Removed MQTT logging
- `src/main.cpp` - Removed setMQTTClient() call
- `platformio.ini` - Changed library dependency
- `src/OTAUpdater.h` - Version bump to 0.36.2

## Upgrade Path
1. Flash v0.36.2 firmware to all devices
2. Devices will automatically connect to HiveMQ Cloud
3. Test offline messaging by rebooting device after sending message
4. Verify message is received on reconnection

---

**This is a significant infrastructure upgrade that enables true asynchronous messaging with offline delivery - a core requirement for the SmolTxt use case.**
