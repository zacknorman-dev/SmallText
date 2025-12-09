# ESP-MQTT Library Migration Plan

## Executive Summary
Migrating from AsyncMqttClient to ESP-MQTT library to enable TLS support for HiveMQ Cloud connection. This will enable reliable long-term offline message queuing (days/weeks/months).

**Date:** January 2025  
**Current Version:** v0.36.3 (AsyncMqttClient with test.mosquitto.org)  
**Target:** ESP-MQTT with HiveMQ Cloud TLS  
**Risk Level:** HIGH - Core messaging functionality rewrite  

---

## Research Findings

### ESP-MQTT Library Overview
- **Source:** Native Espressif library (esp-mqtt component)
- **Built-in:** Part of ESP-IDF framework, no external dependency needed
- **Features:** QoS 0/1/2, TLS/SSL, WebSocket, persistent sessions, MQTT 3.1.1 & 5.0
- **Repository:** https://github.com/espressif/esp-mqtt

### Key API Differences

#### 1. Configuration Structure
**AsyncMqttClient (Current):**
```cpp
mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
mqttClient.setClientId(clientId.c_str());
mqttClient.setCleanSession(false);
mqttClient.onConnect(onMqttConnect);
mqttClient.onDisconnect(onMqttDisconnect);
mqttClient.onMessage(onMqttMessage);
mqttClient.onPublish(onMqttPublish);
```

**ESP-MQTT (Target):**
```cpp
esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
        .address = {
            .uri = "mqtts://your-broker.hivemq.cloud:8883",  // TLS support!
        },
        .verification = {
            .certificate = (const char *)server_cert_pem_start,  // CA cert
        }
    },
    .credentials = {
        .username = "username",
        .client_id = "clientId",
        .authentication = {
            .password = "password"
        }
    },
    .session = {
        .disable_clean_session = false,  // false = persistent session
        .keepalive = 120
    }
};

esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
esp_mqtt_client_start(client);
```

#### 2. Event Handler System
**AsyncMqttClient:** Static callback functions
```cpp
void onMqttConnect(bool sessionPresent) { }
void onMqttMessage(char* topic, char* payload, ...) { }
```

**ESP-MQTT:** Unified event handler with event types
```cpp
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, 
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            // Handle connection
            break;
            
        case MQTT_EVENT_DATA:
            // Handle incoming message
            // event->topic, event->topic_len
            // event->data, event->data_len
            break;
            
        case MQTT_EVENT_PUBLISHED:
            // Message published (QoS 1/2 only)
            // event->msg_id
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            // Subscription confirmed
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            // Handle disconnection
            break;
            
        case MQTT_EVENT_ERROR:
            // Handle errors
            break;
    }
}
```

#### 3. Publish Operations
**AsyncMqttClient:**
```cpp
uint16_t packetId = mqttClient.publish(topic, qos, retain, payload, length);
// Returns packet ID (uint16_t)
```

**ESP-MQTT:**
```cpp
int msg_id = esp_mqtt_client_publish(client, topic, payload, length, qos, retain);
// Returns message ID (int), -1 on error
```

#### 4. Subscribe Operations
**AsyncMqttClient:**
```cpp
uint16_t packetId = mqttClient.subscribe(topic, qos);
```

**ESP-MQTT:**
```cpp
int msg_id = esp_mqtt_client_subscribe(client, topic, qos);
```

#### 5. TLS Configuration
**AsyncMqttClient:** ❌ No native TLS support on ESP32

**ESP-MQTT:** ✅ Full TLS support with multiple options:
```cpp
// Option 1: PEM certificate
.broker.verification.certificate = (const char *)ca_cert_pem_start,

// Option 2: Certificate bundle (recommended for production)
.broker.verification.crt_bundle_attach = esp_crt_bundle_attach,

// Option 3: Global CA store
.broker.verification.use_global_ca_store = true,

// Client certificates for mutual auth
.credentials.authentication = {
    .certificate = (const char *)client_cert_pem_start,
    .key = (const char *)client_key_pem_start,
}
```

---

## Migration Strategy

### Phase 1: Preparation (COMPLETED)
✅ Backup AsyncMqttClient implementation  
✅ Document all current patterns and fixes  
✅ Research ESP-MQTT API  

### Phase 2: Implementation
**Critical Requirements:**
1. Preserve v0.36.3 race condition fixes (storage-first updates)
2. Maintain multi-village encryption routing
3. Preserve status progression (sent→received→read)
4. Enable TLS for HiveMQ Cloud
5. Maintain QoS 1 for reliable delivery

**Implementation Steps:**

#### Step 1: Update platformio.ini
- Remove: `marvinroger/AsyncMqttClient@^0.9.0`
- ESP-MQTT is built-in, no external dependency needed
- Verify no conflicts with other libraries

#### Step 2: MQTTMessenger.h Changes
```cpp
// OLD
#include <AsyncMqttClient.h>
#define MQTT_BROKER "test.mosquitto.org"
#define MQTT_PORT 1883

// NEW
#include "mqtt_client.h"
#define MQTT_BROKER_URI "mqtts://your-cluster.hivemq.cloud:8883"

// Update class members
class MQTTMessenger {
private:
    esp_mqtt_client_handle_t mqttClient;  // Changed from AsyncMqttClient
    
    // Event handler (replaces static callbacks)
    static void mqttEventHandler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data);
                                
    // Keep existing structure for multi-village support
    struct VillageSubscription {
        String villageName;
        String subscriptionTopic;
    };
    std::vector<VillageSubscription> subscriptions;
};
```

#### Step 3: MQTTMessenger.cpp Changes

**Connection Setup:**
```cpp
void MQTTMessenger::begin(/* params */) {
    // Generate client ID (preserve existing logic)
    String clientId = "SmolTxt-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    
    // Configure ESP-MQTT
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;
    mqtt_cfg.credentials.username = username;  // HiveMQ Cloud username
    mqtt_cfg.credentials.client_id = clientId.c_str();
    mqtt_cfg.credentials.authentication.password = password;  // HiveMQ Cloud password
    mqtt_cfg.broker.verification.certificate = (const char *)hivemq_ca_cert_pem_start;
    mqtt_cfg.session.disable_clean_session = false;  // Enable persistent session
    mqtt_cfg.session.keepalive = 120;
    
    // Initialize and start
    mqttClient = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqttClient, ESP_EVENT_ANY_ID, 
                                   mqttEventHandler, this);  // Pass 'this' as context
    esp_mqtt_client_start(mqttClient);
}
```

**Event Handler (replaces all callbacks):**
```cpp
void MQTTMessenger::mqttEventHandler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
    MQTTMessenger* self = (MQTTMessenger*)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            // Replaces onMqttConnect
            Serial.println("[MQTT] Connected to broker");
            
            // Resubscribe to all villages
            for (const auto& sub : self->subscriptions) {
                esp_mqtt_client_subscribe(client, sub.subscriptionTopic.c_str(), 1);
            }
            break;
            
        case MQTT_EVENT_DATA:
            // Replaces onMqttMessage
            String topic(event->topic, event->topic_len);
            String payload(event->data, event->data_len);
            
            // Find which village this message belongs to
            for (const auto& sub : self->subscriptions) {
                if (topic == sub.subscriptionTopic) {
                    // Decrypt and process message
                    // ... (preserve existing decryption logic)
                    
                    // CRITICAL: Preserve v0.36.3 fix
                    // Update storage FIRST, then UI
                    village.updateMessageStatus(msgId, MSG_RECEIVED);
                    ui.updateMessageStatus(msgId, MSG_RECEIVED);
                    break;
                }
            }
            break;
            
        case MQTT_EVENT_PUBLISHED:
            // Replaces onMqttPublish (ACK received)
            // event->msg_id contains the message ID
            
            // CRITICAL: Preserve v0.36.3 fix
            // Update storage FIRST, then UI
            village.updateMessageStatus(event->msg_id, MSG_SENT);
            ui.updateMessageStatus(event->msg_id, MSG_SENT);
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            Serial.printf("[MQTT] Subscribed to topic, msg_id=%d\n", event->msg_id);
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            Serial.println("[MQTT] Disconnected from broker");
            break;
            
        case MQTT_EVENT_ERROR:
            Serial.println("[MQTT] Error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                Serial.printf("  TLS error: %s\n", 
                    esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
            }
            break;
    }
}
```

**Publish Function:**
```cpp
uint16_t MQTTMessenger::publish(const String& topic, const String& payload, uint8_t qos) {
    int msg_id = esp_mqtt_client_publish(mqttClient, 
                                        topic.c_str(),
                                        payload.c_str(),
                                        payload.length(),
                                        qos,
                                        0);  // retain = false
    
    if (msg_id < 0) {
        Serial.println("[MQTT] Publish failed");
        return 0;
    }
    
    return (uint16_t)msg_id;
}
```

**Subscribe Function:**
```cpp
uint16_t MQTTMessenger::subscribe(const String& topic, uint8_t qos) {
    int msg_id = esp_mqtt_client_subscribe(mqttClient, topic.c_str(), qos);
    return (msg_id >= 0) ? (uint16_t)msg_id : 0;
}
```

#### Step 4: HiveMQ Cloud Certificate
Need to obtain HiveMQ Cloud CA certificate and embed it:

1. Download HiveMQ Cloud CA cert (or use Let's Encrypt root CA)
2. Create file: `src/certs/hivemq_ca_cert.pem`
3. Reference in CMakeLists.txt or platformio.ini for embedding
4. Access via: `extern const char hivemq_ca_cert_pem_start[] asm("_binary_hivemq_ca_cert_pem_start");`

---

## Testing Plan

### Test 1: Basic Connectivity
**Objective:** Verify TLS connection to HiveMQ Cloud  
**Steps:**
1. Upload firmware to one device
2. Watch serial output for "Connected to broker"
3. Verify no TLS handshake errors
4. Check HiveMQ Cloud console for connected client

**Success Criteria:**
- ✅ Device connects successfully
- ✅ No SSL/TLS errors in serial output
- ✅ Client appears in HiveMQ dashboard

### Test 2: Publish/Subscribe
**Objective:** Verify QoS 1 messaging works  
**Steps:**
1. Device A publishes test message
2. Device B should receive message
3. Verify MQTT_EVENT_PUBLISHED fires (ACK received)

**Success Criteria:**
- ✅ Messages transmit successfully
- ✅ QoS 1 acknowledgments received
- ✅ No message loss

### Test 3: Message Status Flow
**Objective:** Verify status progression (sent→received→read)  
**Steps:**
1. Device A sends message to Device B
2. Verify Device A shows ✓ (sent)
3. Verify Device B receives and shows ✓✓ (received)
4. Device A should update to ✓✓ when ACK arrives
5. Open conversation on Device B
6. Verify both devices show ✓✓✓ (read)

**Success Criteria:**
- ✅ All three status levels work correctly
- ✅ No race conditions (v0.36.3 fix preserved)
- ✅ No "Message not found" errors

### Test 4: Offline Message Queuing (CRITICAL)
**Objective:** Verify messages queue for extended offline periods  
**Steps:**
1. Device A online, Device B powered off
2. Device A sends 5 messages
3. Wait 10 minutes
4. Device A sends 5 more messages
5. Power on Device B
6. Verify all 10 messages arrive

**Extended Test:**
- Leave Device B offline for 24+ hours
- Send messages periodically from Device A
- Power on Device B
- Verify ALL messages arrive in order

**Success Criteria:**
- ✅ Messages queue reliably on broker
- ✅ All messages delivered when client reconnects
- ✅ Message order preserved
- ✅ Works for extended periods (days/weeks)

### Test 5: Multi-Village Support
**Objective:** Verify multiple village subscriptions work  
**Steps:**
1. Device A joins Village "Alpha" and "Beta"
2. Device B joins only "Alpha"
3. Device C joins only "Beta"
4. Send messages in each village
5. Verify no cross-village message leakage

**Success Criteria:**
- ✅ Each village maintains separate encryption
- ✅ Messages only appear in correct village
- ✅ All subscriptions remain active
- ✅ No performance degradation with multiple villages

### Test 6: Reconnection & Session Persistence
**Objective:** Verify persistent sessions work  
**Steps:**
1. Device connects, subscribes to topics
2. Force disconnect (turn off WiFi router)
3. Send message from other device
4. Restore WiFi connection
5. Verify offline message arrives

**Success Criteria:**
- ✅ Session restored on reconnection
- ✅ Subscriptions persist (no resubscribe needed)
- ✅ Offline messages delivered
- ✅ Client ID consistency maintained

---

## Rollback Plan

**If migration fails:**
1. Revert to AsyncMqttClient code from backup
2. Restore test.mosquitto.org broker configuration
3. Re-upload v0.36.3 firmware to both devices
4. Document failure reason for future attempt

**Backup Files Location:**
- `docs/backup_AsyncMqtt_MQTTMessenger.h`
- `docs/backup_AsyncMqtt_MQTTMessenger.cpp`
- `docs/AsyncMqttClient_Implementation_Backup.md`
- Git commit: 9150a31

---

## Risk Assessment

### HIGH RISKS
1. **Event Handler Conversion:** Different architecture from callbacks
   - *Mitigation:* Careful mapping of each callback to event type
   - *Test:* Extensive testing of each event type

2. **Message ID Tracking:** Different ID format (int vs uint16_t)
   - *Mitigation:* Update all message ID storage to handle int type
   - *Test:* Verify ACK matching still works correctly

3. **Race Conditions:** v0.36.3 fixes must be preserved
   - *Mitigation:* Maintain storage-first update pattern
   - *Test:* Stress test with rapid message sending

### MEDIUM RISKS
1. **Multi-Village Routing:** Event handler must support multiple subscriptions
   - *Mitigation:* Pass 'this' pointer to access subscription list
   - *Test:* Multi-village stress testing

2. **TLS Certificate Management:** Certificate must be valid and embedded correctly
   - *Mitigation:* Verify certificate chain beforehand
   - *Test:* Connection tests with various certificate scenarios

### LOW RISKS
1. **Memory Usage:** Different library may have different footprint
   - *Mitigation:* Monitor heap usage during testing
   - *Test:* Extended runtime memory leak tests

---

## Success Metrics

**Must Have (P0):**
- ✅ TLS connection to HiveMQ Cloud working
- ✅ QoS 1 messaging reliable
- ✅ Status progression (sent→received→read) works
- ✅ Offline messages queue for 24+ hours
- ✅ No regression in v0.36.3 fixes

**Should Have (P1):**
- ✅ Multi-village support functional
- ✅ Connection stability matches or exceeds AsyncMqttClient
- ✅ Session persistence working reliably

**Nice to Have (P2):**
- Memory footprint reduced
- Faster connection establishment
- Better error reporting

---

## Timeline Estimate

1. **Platform prep:** 15 minutes (platformio.ini update)
2. **Header rewrite:** 30 minutes (MQTTMessenger.h)
3. **Implementation rewrite:** 2-3 hours (MQTTMessenger.cpp)
4. **Initial compile/debug:** 30 minutes
5. **Basic testing:** 1 hour (connectivity, pub/sub)
6. **Status flow testing:** 1 hour
7. **Offline queue testing:** 2-4 hours (need extended wait times)
8. **Multi-village testing:** 1 hour
9. **Stress/reliability testing:** 2-4 hours

**Total Estimate:** 10-15 hours

---

## Notes for Implementation

### Key Points to Remember
1. **Always pass 'this' to event handler** to access member variables
2. **Storage-first pattern is CRITICAL** - do not change order
3. **Test incrementally** - don't upload to both devices until basic tests pass
4. **Keep serial logging verbose** during initial testing
5. **Document any API behavior differences** discovered during testing

### Common Pitfalls to Avoid
- ❌ Don't forget to convert String to const char* for ESP-MQTT APIs
- ❌ Don't mix up topic_len with null-terminated strings
- ❌ Don't assume message ID behavior is identical
- ❌ Don't skip certificate validation in production
- ❌ Don't change status update order (storage must be first)

### Reference Examples
ESP-MQTT repository has excellent examples:
- `examples/ssl/` - TLS connection example
- `examples/ssl_mutual_auth/` - Client certificate auth
- `examples/tcp/` - Basic connectivity patterns
- `test/apps/publish_connect_test/` - QoS testing patterns

---

## Post-Migration Validation

After successful migration, verify:
1. Both devices running new firmware
2. All features working (messaging, sync, status)
3. Offline queuing tested for 24+ hours
4. Multi-village operation stable
5. No memory leaks after extended runtime
6. Update firmware version to v0.37.0
7. Create Git release with migration notes
8. Update documentation

---

## Conclusion

This migration is necessary to achieve the user's core requirement: **reliable offline message queuing that works for days/weeks/months**. The ESP-MQTT library provides native TLS support, enabling connection to HiveMQ Cloud which has proven to reliably queue messages.

The main challenges are:
1. Converting callback architecture to event handler system
2. Preserving critical v0.36.3 race condition fixes
3. Ensuring multi-village support continues to work
4. Extensive testing of offline queuing

With careful implementation and thorough testing, this migration should provide significantly improved reliability and unlock the long-term offline messaging capability that has been the goal.
