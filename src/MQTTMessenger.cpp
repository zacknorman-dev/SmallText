#include "MQTTMessenger.h"
#include "Logger.h"
#include <mbedtls/base64.h>

// Let's Encrypt R12 intermediate certificate for HiveMQ Cloud TLS
// HiveMQ Cloud uses: Server cert -> R12 (intermediate) -> ISRG Root X1 (root)
// ESP32 needs the R12 intermediate to complete chain validation
static const char LETSENCRYPT_R12_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFBjCCAu6gAwIBAgIRAMISMktwqbSRcdxA9+KFJjwwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjQwMzEzMDAwMDAw
WhcNMjcwMzEyMjM1OTU5WjAzMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg
RW5jcnlwdDEMMAoGA1UEAxMDUjEyMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB
CgKCAQEA2pgodK2+lP474B7i5Ut1qywSf+2nAzJ+Npfs6DGPpRONC5kuHs0BUT1M
5ShuCVUxqqUiXXL0LQfCTUA83wEjuXg39RplMjTmhnGdBO+ECFu9AhqZ66YBAJpz
kG2Pogeg0JfT2kVhgTU9FPnEwF9q3AuWGrCf4yrqvSrWmMebcas7dA8827JgvlpL
Thjp2ypzXIlhZZ7+7Tymy05v5J75AEaz/xlNKmOzjmbGGIVwx1Blbzt05UiDDwhY
XS0jnV6j/ujbAKHS9OMZTfLuevYnnuXNnC2i8n+cF63vEzc50bTILEHWhsDp7CH4
WRt/uTp8n1wBnWIEwii9Cq08yhDsGwIDAQABo4H4MIH1MA4GA1UdDwEB/wQEAwIB
hjAdBgNVHSUEFjAUBggrBgEFBQcDAgYIKwYBBQUHAwEwEgYDVR0TAQH/BAgwBgEB
/wIBADAdBgNVHQ4EFgQUALUp8i2ObzHom0yteD763OkM0dIwHwYDVR0jBBgwFoAU
ebRZ5nu25eQBc4AIiMgaWPbpm24wMgYIKwYBBQUHAQEEJjAkMCIGCCsGAQUFBzAC
hhZodHRwOi8veDEuaS5sZW5jci5vcmcvMBMGA1UdIAQMMAowCAYGZ4EMAQIBMCcG
A1UdHwQgMB4wHKAaoBiGFmh0dHA6Ly94MS5jLmxlbmNyLm9yZy8wDQYJKoZIhvcN
AQELBQADggIBAI910AnPanZIZTKS3rVEyIV29BWEjAK/duuz8eL5boSoVpHhkkv3
4eoAeEiPdZLj5EZ7G2ArIK+gzhTlRQ1q4FKGpPPaFBSpqV/xbUb5UlAXQOnkHn3m
FVj+qYv87/WeY+Bm4sN3Ox8BhyaU7UAQ3LeZ7N1X01xxQe4wIAAE3JVLUCiHmZL+
qoCUtgYIFPgcg350QMUIWgxPXNGEncT921ne7nluI02V8pLUmClqXOsCwULw+PVO
ZCB7qOMxxMBoCUeL2Ll4oMpOSr5pJCpLN3tRA2s6P1KLs9TSrVhOk+7LX28NMUlI
usQ/nxLJID0RhAeFtPjyOCOscQBA53+NRjSCak7P4A5jX7ppmkcJECL+S0i3kXVU
y5Me5BbrU8973jZNv/ax6+ZK6TM8jWmimL6of6OrX7ZU6E2WqazzsFrLG3o2kySb
zlhSgJ81Cl4tv3SbYiYXnJExKQvzf83DYotox3f0fwv7xln1A2ZLplCb0O+l/AK0
YE0DS2FPxSAHi0iwMfW2nNHJrXcY3LLHD77gRgje4Eveubi2xxa+Nmk/hmhLdIET
iVDFanoCrMVIpQ59XWHkzdFmoHXHBV7oibVjGSO7ULSQ7MJ1Nz51phuDJSgAIU7A
0zrLnOrAj/dfrlEWRhCvAgbuwLZX1A2sjNjXoPOHbsPiy+lO1KF8/XY7
-----END CERTIFICATE-----
)EOF";

extern Logger logger;

MQTTMessenger::MQTTMessenger() {
    mqttClient = nullptr;
    encryption = nullptr;
    currentVillageId = "";
    currentVillageName = "";
    currentUsername = "";
    myMAC = ESP.getEfuseMac();
    onMessageReceived = nullptr;
    onMessageAcked = nullptr;
    onMessageRead = nullptr;
    onCommandReceived = nullptr;
    onSyncRequest = nullptr;
    onVillageNameReceived = nullptr;
    onUsernameReceived = nullptr;
    onInviteReceived = nullptr;
    lastReconnectAttempt = 0;
    lastPingTime = 0;
    lastSeenCleanup = 0;
    connected = false;
    currentSyncPhase = 0;  // Not syncing
    syncTargetMAC = "";
    lastSyncPhaseTime = 0;
    
    // Generate unique client ID from MAC with timestamp to avoid conflicts
    char macStr[13];
    sprintf(macStr, "%012llx", myMAC);
    clientId = "smol_esp_" + String(macStr) + "_" + String(millis());
}

bool MQTTMessenger::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi not connected");
        return false;
    }
    
    Serial.println("[MQTT] Initializing ESP-MQTT messenger");
    Serial.println("[MQTT] Broker: " + String(MQTT_BROKER_URI));
    Serial.println("[MQTT] Username: " + String(MQTT_USERNAME));
    Serial.println("[MQTT] Client ID: " + clientId);
    
    logger.info("MQTT: Broker=" + String(MQTT_BROKER_URI));
    logger.info("MQTT: ClientID=" + clientId);
    if (strlen(MQTT_USERNAME) > 0) {
        logger.info("MQTT: User=" + String(MQTT_USERNAME));
    } else {
        logger.info("MQTT: No authentication (open broker)");
    }
    
    // Configure ESP-MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = MQTT_BROKER_URI;
    mqtt_cfg.client_id = clientId.c_str();
    
    // Only set credentials if provided
    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.username = MQTT_USERNAME;
    }
    if (strlen(MQTT_PASSWORD) > 0) {
        mqtt_cfg.password = MQTT_PASSWORD;
    }
    
    // Only set certificate for TLS connections (mqtts://)
    if (strstr(MQTT_BROKER_URI, "mqtts://") != nullptr) {
        mqtt_cfg.cert_pem = LETSENCRYPT_R12_CERT;
        logger.info("MQTT: TLS enabled with R12 certificate");
    } else {
        logger.info("MQTT: Plain MQTT (no TLS)");
    }
    
    mqtt_cfg.disable_clean_session = 1;  // 1 = disable persistent session (try clean session first)
    mqtt_cfg.keepalive = 60;  // Shorter keepalive
    mqtt_cfg.disable_auto_reconnect = false;  // Enable auto-reconnect
    mqtt_cfg.network_timeout_ms = 10000;  // 10 second timeout
    mqtt_cfg.protocol_ver = MQTT_PROTOCOL_V_3_1_1;  // Explicitly use MQTT 3.1.1
    
    logger.info("MQTT: Config set - clean_session=true keepalive=60");
    
    //  Based on community research: ESP-MQTT TLS has ~20% initial connection failure rate
    // Solution: Retry mechanism (1-2 retries resolves 97% of issues)
    // Source: https://github.com/espressif/esp-mqtt/issues/288
    const int MAX_RETRIES = 3;
    int retry_count = 0;
    bool init_success = false;
    
    while (retry_count < MAX_RETRIES && !init_success) {
        if (retry_count > 0) {
            Serial.printf("[MQTT] Retry attempt %d of %d\n", retry_count, MAX_RETRIES - 1);
            logger.info("MQTT: Retry " + String(retry_count));
            delay(2000 * retry_count);  // Exponential backoff: 2s, 4s, 6s
        }
        
        // Initialize client
        mqttClient = esp_mqtt_client_init(&mqtt_cfg);
        if (!mqttClient) {
            Serial.println("[MQTT] Failed to initialize client");
            retry_count++;
            continue;
        }
        
        // Register event handler (pass 'this' as context to access member variables)
        esp_mqtt_client_register_event(mqttClient, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, 
                                       mqttEventHandler, this);
        
        // Start client
        esp_err_t err = esp_mqtt_client_start(mqttClient);
        if (err != ESP_OK) {
            Serial.printf("[MQTT] Failed to start: %s\n", esp_err_to_name(err));
            esp_mqtt_client_destroy(mqttClient);
            mqttClient = nullptr;
            retry_count++;
            continue;
        }
        
        init_success = true;
    }
    
    if (!init_success) {
        Serial.printf("[MQTT] Failed to initialize after %d retries\n", MAX_RETRIES);
        logger.error("MQTT: Failed after " + String(MAX_RETRIES) + " retries");
        return false;
    }
    
    Serial.printf("[MQTT] Client started (retry attempts: %d)\n", retry_count);
    if (retry_count > 0) {
        logger.info("MQTT: Started after " + String(retry_count) + " retries");
    }
    
    return true;
}

void MQTTMessenger::setEncryption(Encryption* enc) {
    encryption = enc;
}

void MQTTMessenger::setVillageInfo(const String& villageId, const String& villageName, const String& username) {
    currentVillageId = villageId;
    currentVillageName = villageName;
    currentUsername = username;
    Serial.println("[MQTT] Active Village Set:");
    Serial.println("  ID: " + currentVillageId);
    Serial.println("  Name: " + currentVillageName);
    Serial.println("  User: " + currentUsername);
    
    // For backwards compatibility: if this village isn't in subscriptions, add it
    if (!findVillageSubscription(villageId) && encryption) {
        addVillageSubscription(villageId, villageName, username, encryption->getKey());
    } else {
        // Just set it as active
        setActiveVillage(villageId);
    }
}

void MQTTMessenger::setMessageCallback(void (*callback)(const Message& msg)) {
    onMessageReceived = callback;
}

void MQTTMessenger::setAckCallback(void (*callback)(const String& messageId, const String& fromMAC)) {
    onMessageAcked = callback;
}

void MQTTMessenger::setReadCallback(void (*callback)(const String& messageId, const String& fromMAC)) {
    onMessageRead = callback;
}

void MQTTMessenger::setBootSyncCompleteCallback(void (*callback)()) {
    onBootSyncComplete = callback;
}

void MQTTMessenger::setCommandCallback(void (*callback)(const String& command)) {
    onCommandReceived = callback;
}

void MQTTMessenger::setSyncRequestCallback(void (*callback)(const String& requestorMAC, unsigned long timestamp)) {
    onSyncRequest = callback;
}

void MQTTMessenger::setVillageNameCallback(void (*callback)(const String& villageId, const String& villageName)) {
    onVillageNameReceived = callback;
}

void MQTTMessenger::setUsernameCallback(void (*callback)(const String& villageId, const String& username)) {
    onUsernameReceived = callback;
}

void MQTTMessenger::setInviteCallback(void (*callback)(const String& villageId, const String& villageName, const uint8_t* encryptedKey, size_t keyLen, int conversationType, const String& creatorUsername)) {
    onInviteReceived = callback;
}

String MQTTMessenger::generateMessageId() {
    static uint32_t counter = 0;
    counter++;
    char id[17];
    sprintf(id, "%08lx%08x", millis(), counter);
    return String(id);
}

String MQTTMessenger::generateTopic(const String& messageType, const String& target) {
    // Topic structure: smoltxt/{villageId}/{messageType}[/{target}]
    String topic = "smoltxt/" + currentVillageId + "/" + messageType;
    if (!target.isEmpty()) {
        topic += "/" + target;
    }
    return topic;
}

bool MQTTMessenger::reconnect() {
    // ESP-MQTT handles reconnection automatically
    // This function is kept for API compatibility but doesn't need to do anything
    return mqttClient != nullptr;
}

void MQTTMessenger::loop() {
    // ESP-MQTT handles all connection management internally via FreeRTOS task
    // No explicit loop processing needed
    
    // Background sync phase continuation
    // If Phase 1 is complete and more history exists, request next phase after delay
    unsigned long now = millis();
    if (currentSyncPhase > 1 && !syncTargetMAC.isEmpty()) {
        // Wait 5 seconds between background phases to not overwhelm the device
        if (now - lastSyncPhaseTime > 5000) {
            Serial.println("[MQTT] Requesting background sync Phase " + String(currentSyncPhase));
            
            // Send sync request with phase number (using timestamp field as hack)
            requestSync(currentSyncPhase);  // Phase number passed as timestamp
            
            // Reset state so we don't spam requests
            syncTargetMAC = "";
            currentSyncPhase = 0;
        }
    }
    
    // Cleanup old seen messages (every 5 minutes)
    if (now - lastSeenCleanup > 300000) {
        cleanupSeenMessages();
        lastSeenCleanup = now;
    }
}

void MQTTMessenger::cleanupSeenMessages() {
    // Keep seen messages for deduplication
    // For now, just clear all (in production, could use timestamp-based cleanup)
    if (seenMessageIds.size() > 100) {
        Serial.println("[MQTT] Clearing old seen message IDs (" + String(seenMessageIds.size()) + " entries)");
        seenMessageIds.clear();
    }
    
    // Clean up processed ACKs/receipts maps
    if (processedAcks.size() > 100) {
        Serial.println("[MQTT] Clearing old processed ACKs (" + String(processedAcks.size()) + " entries)");
        processedAcks.clear();
    }
    if (processedReadReceipts.size() > 100) {
        Serial.println("[MQTT] Clearing old processed read receipts (" + String(processedReadReceipts.size()) + " entries)");
        processedReadReceipts.clear();
    }
}

// ESP-MQTT unified event handler
void MQTTMessenger::mqttEventHandler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
    MQTTMessenger* self = (MQTTMessenger*)handler_args;
    if (!self) return;
    
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED: {
            Serial.println("[MQTT] Connected to broker!");
            Serial.println("[MQTT] Session present: " + String(event->session_present ? "yes" : "no"));
            self->connected = true;
            
            // Load and subscribe to all saved villages
            Serial.println("[MQTT] Loading saved villages for subscription...");
            self->subscribeToAllVillages();
            
            // Subscribe to all saved villages with QoS 1
            if (self->subscribedVillages.size() > 0) {
                for (const auto& village : self->subscribedVillages) {
                    String baseTopic = "smoltxt/" + village.villageId + "/#";
                    esp_mqtt_client_subscribe(client, baseTopic.c_str(), 1);  // QoS 1
                    Serial.println("[MQTT] Subscribed to: " + baseTopic + " (" + village.villageName + ")");
                }
                logger.info("MQTT: Connected - subscribed to " + String(self->subscribedVillages.size()) + " villages");
            } else {
                Serial.println("[MQTT] Warning: No villages to subscribe to");
            }
            
            // Subscribe to device command topic
            char macStr[13];
            sprintf(macStr, "%012llx", self->myMAC);
            String commandTopic = "smoltxt/" + String(macStr) + "/command";
            esp_mqtt_client_subscribe(client, commandTopic.c_str(), 1);
            Serial.println("[MQTT] Subscribed to command topic: " + commandTopic);
            
            // Subscribe to sync response topic
            String syncResponseTopic = "smoltxt/" + String(macStr) + "/sync-response";
            esp_mqtt_client_subscribe(client, syncResponseTopic.c_str(), 1);
            Serial.println("[MQTT] Subscribed to sync response topic: " + syncResponseTopic);
            
            // Wait for MQTT subscription to be fully established on broker, then request boot sync
            if (self->subscribedVillages.size() > 0) {
                Serial.println("[MQTT] Waiting 2s for subscriptions to be established...");
                vTaskDelay(pdMS_TO_TICKS(2000));  // Use FreeRTOS delay in MQTT event handler
                Serial.println("[MQTT] Requesting boot sync for all subscribed villages...");
                
                // Request sync for each subscribed village
                for (const auto& village : self->subscribedVillages) {
                    self->requestSync(0);  // Sync all messages (timestamp 0)
                    Serial.println("[MQTT] Boot sync requested for village: " + village.villageName);
                }
                logger.info("MQTT: Boot sync requested for " + String(self->subscribedVillages.size()) + " villages");
            }
            break;
        }
            
        case MQTT_EVENT_DISCONNECTED:
            Serial.println("[MQTT] Disconnected from broker");
            self->connected = false;
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            Serial.printf("[MQTT] Subscribed to topic, msg_id=%d\n", event->msg_id);
            break;
            
        case MQTT_EVENT_DATA: {
            // Handle incoming message
            String topic(event->topic, event->topic_len);
            Serial.printf("[MQTT] EVENT_DATA: topic=%.*s len=%d session_present=%d\n", 
                event->topic_len, event->topic, event->data_len, event->session_present);
            self->handleIncomingMessage(topic, (const uint8_t*)event->data, event->data_len);
            break;
        }
            
        case MQTT_EVENT_PUBLISHED:
            // QoS 1 ACK received for our published message
            Serial.printf("[MQTT] Message published successfully, msg_id=%d\n", event->msg_id);
            break;
            
        case MQTT_EVENT_ERROR:
            Serial.println("[MQTT] Error occurred");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                Serial.printf("  Transport error type: TCP_TRANSPORT\n");
                Serial.printf("  TLS stack error: 0x%x\n", event->error_handle->esp_tls_stack_err);
                Serial.printf("  TLS cert verify error: 0x%x\n", event->error_handle->esp_tls_cert_verify_flags);
                Serial.printf("  ESP-TLS error: %s (0x%x)\n", 
                    esp_err_to_name(event->error_handle->esp_tls_last_esp_err),
                    event->error_handle->esp_tls_last_esp_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                Serial.printf("  Connection refused\n");
            }
            self->connected = false;
            break;
            
        default:
            Serial.printf("[MQTT] Other event id:%d\n", event_id);
            break;
    }
}

// OLD AsyncMqttClient callbacks - REMOVED
// void MQTTMessenger::onMqttConnect(bool sessionPresent) { ... }
// void MQTTMessenger::onMqttDisconnect(AsyncMqttClientDisconnectReason reason) { ... }
// void MQTTMessenger::onMqttMessage(char* topic, char* payload, ...) { ... }

void MQTTMessenger::handleIncomingMessage(const String& topic, const uint8_t* payload, unsigned int length) {
    Serial.println("[MQTT] Received on topic: " + topic + " (uptime: " + String(millis()) + "ms)");
    Serial.println("[MQTT] seenMessageIds size: " + String(seenMessageIds.size()));
    
    // Check if this is a command message
    char macStr[13];
    sprintf(macStr, "%012llx", myMAC);
    String commandTopic = "smoltxt/" + String(macStr) + "/command";
    
    if (topic == commandTopic) {
        // Command messages are plain text, not encrypted
        String command = "";
        for (unsigned int i = 0; i < length; i++) {
            command += (char)payload[i];
        }
        Serial.println("[MQTT] Received command: " + command);
        logger.info("MQTT command: " + command);
        
        if (onCommandReceived) {
            onCommandReceived(command);
        }
        return;
    }
    
    // Check for sync-response topic (addressed to us)
    String syncResponseTopic = "smoltxt/" + String(macStr) + "/sync-response";
    if (topic == syncResponseTopic) {
        handleSyncResponse(payload, length);
        return;
    }
    
    // Extract villageId from topic: smoltxt/{villageId}/...
    int firstSlash = topic.indexOf('/');
    int secondSlash = topic.indexOf('/', firstSlash + 1);
    if (firstSlash == -1 || secondSlash == -1) {
        Serial.println("[MQTT] Invalid topic format");
        return;
    }
    
    String villageId = topic.substring(firstSlash + 1, secondSlash);
    Serial.println("[MQTT] Message for village: " + villageId);
    
    // Check for invite code topics (smoltxt/invites/{code})
    if (topic.startsWith("smoltxt/invites/")) {
        String inviteCode = topic.substring(16);  // Skip "smoltxt/invites/"
        Serial.println("[MQTT] ====== INVITE DATA RECEIVED ======");
        Serial.println("[MQTT] Received invite data for code: " + inviteCode);
        Serial.println("[MQTT] Payload length: " + String(length));
        
        // Parse JSON payload
        String message = "";
        for (unsigned int i = 0; i < length; i++) {
            message += (char)payload[i];
        }
        Serial.println("[MQTT] Invite payload: " + message);
        
        if (message.length() == 0) {
            Serial.println("[MQTT] Empty invite payload (unpublished/cleared)");
            return;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, message);
        if (error) {
            Serial.println("[MQTT] Invite JSON parse error: " + String(error.c_str()));
            return;
        }
        
        String inviteVillageId = doc["villageId"] | "";
        String inviteVillageName = doc["villageName"] | "";
        String encodedKey = doc["key"] | "";
        int conversationType = doc["type"] | 0;  // Default to 0 (GROUP) if not present
        String creatorUsername = doc["creatorUsername"] | "";  // Get creator's username
        
        Serial.println("[MQTT] Parsed - Name: " + inviteVillageName + ", ID: " + inviteVillageId + ", Type: " + String(conversationType));
        if (!creatorUsername.isEmpty()) {
            Serial.println("[MQTT] Creator username: " + creatorUsername);
        }
        Serial.println("[MQTT] Encoded key length: " + String(encodedKey.length()));
        
        // Decode the encryption key from base64
        uint8_t decodedKey[32];
        size_t decodedLen = 0;
        mbedtls_base64_decode(decodedKey, sizeof(decodedKey), &decodedLen, 
                            (const unsigned char*)encodedKey.c_str(), encodedKey.length());
        
        if (decodedLen == 32) {
            Serial.println("[MQTT] Invite received: " + inviteVillageName + " (" + inviteVillageId + ") type=" + String(conversationType));
            logger.info("Invite received: " + inviteVillageName);
            
            if (onInviteReceived) {
                Serial.println("[MQTT] Calling onInviteReceived callback");
                onInviteReceived(inviteVillageId, inviteVillageName, decodedKey, 32, conversationType, creatorUsername);
            } else {
                Serial.println("[MQTT] WARNING: No onInviteReceived callback set!");
            }
        } else {
            Serial.println("[MQTT] Invite key decode failed: wrong length " + String(decodedLen));
        }
        Serial.println("[MQTT] ===================================");
        return;
    }
    
    // Check for village name announcement (unencrypted, just the name)
    if (topic.endsWith("/villagename")) {
        String villageName = "";
        for (unsigned int i = 0; i < length; i++) {
            villageName += (char)payload[i];
        }
        Serial.println("[MQTT] Received village name announcement: " + villageName + " for village: " + villageId);
        logger.info("Village name received: " + villageName + " (ID: " + villageId + ")");
        
        if (onVillageNameReceived) {
            onVillageNameReceived(villageId, villageName);
        }
        return;
    }
    
    // Check for username announcement (smoltxt/{villageId}/username/{MAC})
    if (topic.indexOf("/username/") > 0) {
        String username = "";
        for (unsigned int i = 0; i < length; i++) {
            username += (char)payload[i];
        }
        
        Serial.println("[MQTT] Received username announcement: " + username + " for village: " + villageId);
        logger.info("Username received: " + username + " (Village ID: " + villageId + ")");
        
        if (onUsernameReceived) {
            onUsernameReceived(villageId, username);
        }
        return;
    }
    
    // Check for sync-request topics (from other devices)
    if (topic.startsWith("smoltxt/" + villageId + "/sync-request/")) {
        handleSyncRequest(villageId, payload, length);
        return;
    }
    
    // Find the village subscription to get the encryption key
    VillageSubscription* village = findVillageSubscription(villageId);
    if (!village) {
        Serial.println("[MQTT] Village not found in subscriptions: " + villageId);
        return;
    }
    
    // Create temporary encryption object with this village's key
    Encryption tempEncryption;
    tempEncryption.setKey(village->encryptionKey);
    
    // Decrypt payload
    String message;
    
    if (!tempEncryption.decryptString(payload, length, message)) {
        Serial.println("[MQTT] Decryption failed for village: " + village->villageName);
        logger.error("MQTT: Decryption failed for " + village->villageName);
        return;
    }
    
    Serial.println("[MQTT] Decrypted message from " + village->villageName + ": " + message);
    
    // Parse message format: TYPE:villageId:target:sender:senderMAC:msgId:content:hop:maxHop
    ParsedMessage msg = parseMessage(message);
    
    if (msg.type == MSG_UNKNOWN) {
        Serial.println("[MQTT] Failed to parse message");
        return;
    }
    
    // Check if we've seen this message before
    if (seenMessageIds.find(msg.messageId) != seenMessageIds.end()) {
        Serial.println("[MQTT] Duplicate message, ignoring: " + msg.messageId + " (in seenMessageIds cache)");
        return;
    }
    Serial.println("[MQTT] NEW message: " + msg.messageId + " - adding to seenMessageIds (size: " + String(seenMessageIds.size()) + ")");
    seenMessageIds.insert(msg.messageId);
    
    // Normalize our MAC for comparison
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Handle ACK messages
    if (msg.type == MSG_ACK && msg.target == myMacStr) {
        // Check if we've already processed an ACK for this original message
        auto it = processedAcks.find(msg.content);
        if (it != processedAcks.end()) {
            Serial.println("[MQTT] Duplicate ACK for message " + msg.content + ", ignoring (already processed ACK " + it->second + ")");
            return;
        }
        
        // Track this ACK to prevent duplicates
        processedAcks[msg.content] = msg.messageId;
        
        Serial.println("[MQTT] Received ACK for message: " + msg.content);
        if (onMessageAcked) {
            onMessageAcked(msg.content, msg.senderMAC);
        }
        return;
    }
    
    // Handle read receipts
    if (msg.type == MSG_READ_RECEIPT && msg.target == myMacStr) {
        // Check if we've already processed a read receipt for this original message
        auto it = processedReadReceipts.find(msg.content);
        if (it != processedReadReceipts.end()) {
            Serial.println("[MQTT] Duplicate read receipt for message " + msg.content + ", ignoring (already processed receipt " + it->second + ")");
            return;
        }
        
        // Track this receipt to prevent duplicates
        processedReadReceipts[msg.content] = msg.messageId;
        
        Serial.println("[MQTT] Received read receipt for message: " + msg.content);
        if (onMessageRead) {
            onMessageRead(msg.content, msg.senderMAC);
        }
        return;
    }
    
    // Handle regular messages (SHOUT or WHISPER)
    if (msg.type == MSG_SHOUT || (msg.type == MSG_WHISPER && msg.target == myMacStr)) {
        // Don't process our own messages
        if (msg.senderMAC == myMacStr) {
            Serial.println("[MQTT] Ignoring our own message");
            return;
        }
        
        // Send ACK
        Serial.println("[MQTT] *** SENDING ACK *** for message: " + msg.messageId + " to " + msg.senderMAC + " (uptime: " + String(millis()) + "ms)");
        bool ackSent = sendAck(msg.messageId, msg.senderMAC, msg.villageId);
        Serial.println("[MQTT] ACK send result: " + String(ackSent ? "SUCCESS" : "FAILED"));
        
        // Deliver message to app
        if (onMessageReceived) {
            extern unsigned long getCurrentTime();
            
            // Find which village this message is for
            VillageSubscription* msgVillage = findVillageSubscription(msg.villageId);
            
            Message m;
            m.sender = msg.senderName;
            m.senderMAC = msg.senderMAC;
            m.content = msg.content;
            m.timestamp = getCurrentTime();
            m.villageId = msg.villageId;  // Pass village ID for multi-village support
            
            // Determine if this is a received message or our own sent message
            // Compare sender MAC address with our MAC address (NOT username!)
            if (msg.senderMAC == myMacStr) {
                // This is OUR message (synced back from another device)
                m.received = false;
                m.status = MSG_SENT;  // Our sent messages start as MSG_SENT
                Serial.println("[MQTT] Received our own sent message: " + msg.messageId);
            } else {
                // This is someone else's message
                m.received = true;
                m.status = MSG_RECEIVED;
                Serial.println("[MQTT] Received message from " + msg.senderName);
            }
            
            m.messageId = msg.messageId;
            onMessageReceived(m);
        }
    }
}

ParsedMessage MQTTMessenger::parseMessage(const String& decrypted) {
    ParsedMessage msg;
    
    // Format: TYPE:villageId:target:sender:senderMAC:msgId:content:hop:maxHop
    int idx = 0;
    String parts[9];
    int partIdx = 0;
    
    for (int i = 0; i < decrypted.length() && partIdx < 9; i++) {
        if (decrypted[i] == ':') {
            partIdx++;
        } else {
            parts[partIdx] += decrypted[i];
        }
    }
    
    if (partIdx < 7) {
        return msg;  // Invalid format
    }
    
    // Parse type
    if (parts[0] == "SHOUT") msg.type = MSG_SHOUT;
    else if (parts[0] == "WHISPER") msg.type = MSG_WHISPER;
    else if (parts[0] == "ACK") msg.type = MSG_ACK;
    else if (parts[0] == "READ_RECEIPT") msg.type = MSG_READ_RECEIPT;
    else return msg;
    
    msg.villageId = parts[1];
    msg.target = parts[2];
    msg.senderName = parts[3];
    msg.senderMAC = parts[4];
    msg.messageId = parts[5];
    msg.content = parts[6];
    msg.currentHop = parts[7].toInt();
    msg.maxHop = parts[8].toInt();
    
    return msg;
}

bool MQTTMessenger::announceVillageName(const String& villageName) {
    if (!isConnected() || currentVillageId.isEmpty()) {
        Serial.println("[MQTT] Cannot announce: not connected or no active village");
        return false;
    }
    
    // Topic: smoltxt/{villageId}/villagename
    String topic = "smoltxt/" + currentVillageId + "/villagename";
    
    // Payload is just the village name (no encryption needed - derived from same password)
    // Use retained flag so MQTT broker keeps it for new subscribers
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), villageName.c_str(), 
                                        villageName.length(), 1, 1);  // QoS 1, retain=1
    if (msg_id >= 0) {
        Serial.println("[MQTT] Village name announced (retained, QoS 1): " + villageName);
        return true;
    }
    
    Serial.println("[MQTT] Failed to announce village name");
    return false;
}

bool MQTTMessenger::announceUsername(const String& username) {
    if (!isConnected() || currentVillageId.isEmpty()) {
        Serial.println("[MQTT] Cannot announce username: not connected or no active village");
        return false;
    }
    
    // Topic: smoltxt/{villageId}/username/{myMAC}
    char macStr[13];
    sprintf(macStr, "%012llx", myMAC);
    String topic = "smoltxt/" + currentVillageId + "/username/" + String(macStr);
    
    // Payload is the username
    // Use retained flag so it's available when the other user joins
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), username.c_str(), 
                                        username.length(), 1, 1);  // QoS 1, retain=1
    if (msg_id >= 0) {
        Serial.println("[MQTT] Username announced (retained, QoS 1): " + username);
        return true;
    }
    
    Serial.println("[MQTT] Failed to announce username");
    return false;
}

String MQTTMessenger::sendShout(const String& message) {
    if (!isConnected() || !encryption) {
        Serial.println("[MQTT] Not connected or no encryption");
        return "";
    }
    
    String msgId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: SHOUT:villageId:*:sender:senderMAC:msgId:content:0:0
    String formatted = "SHOUT:" + currentVillageId + ":*:" + currentUsername + ":" + 
                      myMacStr + ":" + msgId + ":" + message + ":0:0";
    
    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        Serial.println("[MQTT] Encryption failed");
        return "";
    }
    
    // Publish to shout topic with QoS 1
    String topic = generateTopic("shout");
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), (const char*)encrypted, 
                                        encryptedLen, 1, 0);  // QoS 1, retain=0
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] SHOUT sent (QoS 1): " + message);
        logger.info("MQTT SHOUT sent: " + message);
        return msgId;
    } else {
        Serial.println("[MQTT] Publish failed");
        return "";
    }
}

String MQTTMessenger::sendSystemMessage(const String& message, const String& systemName) {
    if (!isConnected() || !encryption) {
        Serial.println("[MQTT] Not connected or no encryption");
        return "";
    }
    
    String msgId = generateMessageId();
    
    // Format: SHOUT:villageId:*:systemName:system:msgId:content:0:0
    // Use "system" as MAC address to indicate it's a system message
    String formatted = "SHOUT:" + currentVillageId + ":*:" + systemName + ":system:" + 
                      msgId + ":" + message + ":0:0";
    
    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        Serial.println("[MQTT] Encryption failed");
        return "";
    }
    
    // Publish to shout topic with QoS 1
    String topic = generateTopic("shout");
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), (const char*)encrypted, 
                                        encryptedLen, 1, 0);  // QoS 1, retain=0
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] SYSTEM message sent from " + systemName + ": " + message);
        logger.info("MQTT SYSTEM sent: " + message);
        return msgId;
    } else {
        Serial.println("[MQTT] Publish failed");
        return "";
    }
}

String MQTTMessenger::sendWhisper(const String& recipientMAC, const String& message) {
    if (!isConnected() || !encryption) {
        Serial.println("[MQTT] Not connected or no encryption");
        return "";
    }
    
    String msgId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: WHISPER:villageId:recipientMAC:sender:senderMAC:msgId:content:0:0
    String formatted = "WHISPER:" + currentVillageId + ":" + recipientMAC + ":" +
                      currentUsername + ":" + myMacStr + ":" + msgId + ":" + message + ":0:0";    // Encrypt
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        Serial.println("[MQTT] Encryption failed");
        return "";
    }
    
    // Publish to whisper topic for specific recipient with QoS 1
    String topic = generateTopic("whisper", recipientMAC);
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), (const char*)encrypted, 
                                        encryptedLen, 1, 0);  // QoS 1, retain=0
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] WHISPER sent (QoS 1) to " + recipientMAC + ": " + message);
        logger.info("MQTT WHISPER sent: " + message);
        return msgId;
    } else {
        Serial.println("[MQTT] Publish failed");
        return "";
    }
}

bool MQTTMessenger::sendAck(const String& messageId, const String& targetMAC, const String& villageId) {
    if (!isConnected()) {
        return false;
    }
    
    // Find the village subscription to get encryption key and username
    VillageSubscription* village = findVillageSubscription(villageId);
    if (!village) {
        Serial.println("[MQTT] Cannot send ACK - village not found: " + villageId);
        return false;
    }
    
    // Use temporary encryption with this village's key
    Encryption tempEncryption;
    tempEncryption.setKey(village->encryptionKey);
    
    String ackId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: ACK:villageId:targetMAC:sender:senderMAC:ackId:originalMessageId:0:0
    String formatted = "ACK:" + villageId + ":" + targetMAC + ":" +
                      village->username + ":" + myMacStr + ":" + ackId + ":" + messageId + ":0:0";
    
    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!tempEncryption.encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        return false;
    }
    
    // Generate topic using the message's villageId
    String topic = "smoltxt/" + villageId + "/ack/" + targetMAC;
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), (const char*)encrypted, 
                                        encryptedLen, 1, 0);  // QoS 1, retain=0
    return msg_id >= 0;
}

bool MQTTMessenger::sendReadReceipt(const String& messageId, const String& targetMAC) {
    if (!isConnected() || !encryption) {
        return false;
    }
    
    String readId = generateMessageId();
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    // Format: READ_RECEIPT:villageId:targetMAC:sender:senderMAC:readId:originalMessageId:0:0
    String formatted = "READ_RECEIPT:" + currentVillageId + ":" + targetMAC + ":" +
                      currentUsername + ":" + myMacStr + ":" + readId + ":" + messageId + ":0:0";    uint8_t encrypted[MAX_CIPHERTEXT];
    size_t encryptedLen;
    if (!encryption->encryptString(formatted, encrypted, MAX_CIPHERTEXT, &encryptedLen)) {
        return false;
    }
    
    String topic = generateTopic("read", targetMAC);
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), (const char*)encrypted, 
                                        encryptedLen, 1, 0);  // QoS 1, retain=0
    return msg_id >= 0;
}

bool MQTTMessenger::requestSync(unsigned long lastMessageTimestamp) {
    if (!connected || !mqttClient) {
        Serial.println("[MQTT] Cannot request sync - not connected");
        logger.error("Sync request failed: not connected");
        return false;
    }
    
    // Publish sync request to village topic
    // Timestamp ignored - will send all messages and rely on deduplication
    // Format: sync-request/{deviceMAC}
    // Payload: {mac: myMAC}
    
    JsonDocument doc;
    doc["mac"] = String(myMAC, HEX);
    doc["timestamp"] = lastMessageTimestamp;  // Keep for backwards compat but ignored
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println("[MQTT] Sync request payload: " + payload);
    
    // Encrypt the sync request
    uint8_t encrypted[256];
    int encryptedLen = encryption->encrypt((uint8_t*)payload.c_str(), payload.length(), encrypted, sizeof(encrypted));
    
    if (encryptedLen <= 0) {
        Serial.println("[MQTT] Sync request encryption failed");
        logger.error("Sync encryption failed");
        return false;
    }
    
    String topic = "smoltxt/" + currentVillageId + "/sync-request/" + String(myMAC, HEX);
    Serial.println("[MQTT] Publishing sync request to: " + topic);
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), (const char*)encrypted, 
                                        encryptedLen, 1, 0);  // QoS 1, retain=0
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] Sync request sent (QoS 1, will receive all messages, dedup on receive)");
        logger.info("Sync request sent");
    } else {
        Serial.println("[MQTT] Sync request failed");
        logger.error("Sync request publish failed");
    }
    
    return (msg_id >= 0);
}

bool MQTTMessenger::sendSyncResponse(const String& targetMAC, const std::vector<Message>& messages, int phase) {
    if (!connected || !mqttClient) {
        Serial.println("[MQTT] Cannot send sync response - not connected");
        return false;
    }
    
    if (messages.empty()) {
        Serial.println("[MQTT] No messages to sync");
        return true;
    }
    
    // Get village ID from the first message to find correct encryption key
    String villageId = messages.empty() ? currentVillageId : messages[0].villageId;
    VillageSubscription* village = findVillageSubscription(villageId);
    if (!village) {
        Serial.println("[MQTT] Cannot send sync - village not found: " + villageId);
        return false;
    }
    
    // Create temporary Encryption object with village key
    Encryption villageEncryption;
    villageEncryption.setKey(village->encryptionKey);
    
    // BATCHED SYNC: Send only 20 messages per phase, starting with most recent
    const int MESSAGES_PER_PHASE = 20;
    int totalMessages = messages.size();
    
    // Calculate which 20 messages to send for this phase
    // Phase 1: Last 20 (most recent)
    // Phase 2: Messages 21-40
    // Phase 3: Messages 41-60, etc.
    int startIdx = max(0, totalMessages - (phase * MESSAGES_PER_PHASE));
    int endIdx = totalMessages - ((phase - 1) * MESSAGES_PER_PHASE);
    
    // Extract the slice for this phase
    std::vector<Message> phaseMessages;
    for (int i = startIdx; i < endIdx && i < totalMessages; i++) {
        phaseMessages.push_back(messages[i]);
    }
    
    if (phaseMessages.empty()) {
        Serial.println("[MQTT] Phase " + String(phase) + " complete - no more messages");
        logger.info("Sync phase " + String(phase) + " complete");
        return true;
    }
    
    Serial.println("[MQTT] Sync Phase " + String(phase) + ": Sending " + String(phaseMessages.size()) + " messages (" + String(startIdx) + "-" + String(endIdx-1) + " of " + String(totalMessages) + ") to " + targetMAC);
    logger.info("Sync phase " + String(phase) + ": " + String(phaseMessages.size()) + " msgs");
    
    // Send messages in batches to avoid payload size limits
    const int BATCH_SIZE = 1;  // Reduced to 1 to ensure delivery
    int totalSent = 0;
    
    for (size_t i = 0; i < phaseMessages.size(); i += BATCH_SIZE) {
        JsonDocument doc;
        JsonArray msgArray = doc["messages"].to<JsonArray>();
        
        // Add up to BATCH_SIZE messages
        for (size_t j = i; j < phaseMessages.size() && j < i + BATCH_SIZE; j++) {
            JsonObject msgObj = msgArray.add<JsonObject>();
            msgObj["sender"] = phaseMessages[j].sender;
            msgObj["senderMAC"] = phaseMessages[j].senderMAC;
            msgObj["content"] = phaseMessages[j].content;
            msgObj["timestamp"] = phaseMessages[j].timestamp;
            msgObj["messageId"] = phaseMessages[j].messageId;
            // FIXED: Don't send received/status - receiver determines these based on senderMAC
            // msgObj["received"] = phaseMessages[j].received;
            // msgObj["status"] = (int)phaseMessages[j].status;
            msgObj["villageId"] = phaseMessages[j].villageId;  // Include village ID for multi-village support
        }
        
        doc["batch"] = (i / BATCH_SIZE) + 1;
        doc["total"] = (phaseMessages.size() + BATCH_SIZE - 1) / BATCH_SIZE;
        doc["phase"] = phase;  // Include phase number in payload
        doc["morePhases"] = (startIdx > 0);  // Indicate if more history available
        
        String payload;
        serializeJson(doc, payload);
        
        // Encrypt using village-specific key
        uint8_t encrypted[600];  // MAX_CIPHERTEXT = 512+12+16 = 540 bytes
        int encryptedLen = villageEncryption.encrypt((uint8_t*)payload.c_str(), payload.length(), encrypted, sizeof(encrypted));
        
        if (encryptedLen <= 0) {
            Serial.println("[MQTT] Sync response encryption failed");
            return false;
        }
        
        String topic = "smoltxt/" + targetMAC + "/sync-response";
        int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), (const char*)encrypted, 
                                            encryptedLen, 1, 0);  // QoS 1, retain=0
        
        if (msg_id >= 0) {
            totalSent += min(BATCH_SIZE, (int)(phaseMessages.size() - i));
            Serial.println("[MQTT] Phase " + String(phase) + " batch " + String((i / BATCH_SIZE) + 1) + "/" + String((phaseMessages.size() + BATCH_SIZE - 1) / BATCH_SIZE) + " sent");
            logger.info("Sync batch " + String((i / BATCH_SIZE) + 1) + " sent");
            delay(100);  // Brief delay between batches
        } else {
            Serial.println("[MQTT] Sync batch failed");
            logger.error("Sync batch " + String((i / BATCH_SIZE) + 1) + " failed");
            return false;
        }
    }
    
    return true;
}

void MQTTMessenger::handleSyncRequest(const String& villageId, const uint8_t* payload, unsigned int length) {
    Serial.println("[MQTT] Received sync request for village: " + villageId);
    
    // Find the village subscription to get the correct encryption key
    VillageSubscription* village = findVillageSubscription(villageId);
    if (!village) {
        Serial.println("[MQTT] Ignoring sync request for non-subscribed village: " + villageId);
        return;
    }
    
    // Create temporary Encryption object with village key
    Encryption villageEncryption;
    villageEncryption.setKey(village->encryptionKey);
    
    String message;
    if (!villageEncryption.decryptString(payload, length, message)) {
        Serial.println("[MQTT] Sync request decryption failed for village: " + villageId);
        logger.error("Sync request decrypt failed");
        return;
    }
    
    Serial.println("[MQTT] Decrypted sync request: " + message);
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.println("[MQTT] Sync request parse error: " + String(error.c_str()));
        logger.error("Sync request JSON error");
        return;
    }
    
    unsigned long requestedTimestamp = doc["timestamp"] | 0;
    String requestorMAC = doc["mac"] | "";
    
    Serial.println("[MQTT] Sync request from " + requestorMAC + " for messages after timestamp " + String(requestedTimestamp));
    logger.info("Sync from " + requestorMAC + " ts=" + String(requestedTimestamp));
    
    // Trigger callback to main app to handle sync
    if (onSyncRequest) {
        onSyncRequest(requestorMAC, requestedTimestamp);
    } else {
        Serial.println("[MQTT] No sync request callback set!");
    }
}

void MQTTMessenger::handleSyncResponse(const uint8_t* payload, unsigned int length) {
    Serial.println("[MQTT] ============================================");
    Serial.println("[MQTT] SYNC RESPONSE RECEIVED - length=" + String(length) + " bytes");
    Serial.println("[MQTT] ============================================");
    Serial.println("[MQTT] Decrypting sync response...");
    logger.info("Sync response received: " + String(length) + " bytes");
    
    if (!encryption) {
        Serial.println("[MQTT] No encryption set");
        return;
    }
    
    String message;
    if (!encryption->decryptString(payload, length, message)) {
        Serial.println("[MQTT] Sync response decryption failed");
        logger.error("Sync response decrypt failed");
        return;
    }
    
    Serial.println("[MQTT] Decrypted sync response: " + message.substring(0, 100) + "...");
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    
    if (error) {
        Serial.println("[MQTT] Sync response parse error: " + String(error.c_str()));
        logger.error("Sync response JSON error");
        return;
    }
    
    int batch = doc["batch"] | 0;
    int total = doc["total"] | 0;
    int phase = doc["phase"] | 1;
    bool morePhases = doc["morePhases"] | false;
    
    Serial.println("[MQTT] Sync phase " + String(phase) + " batch " + String(batch) + "/" + String(total));
    logger.info("Sync phase " + String(phase) + " batch " + String(batch) + "/" + String(total));
    
    // OPTIMIZATION: Set global sync flag to skip expensive status updates during sync
    // This is a global from main.cpp - forward declaration needed
    extern bool isSyncing;
    if (batch == 1 && phase == 1) {
        isSyncing = true;  // Start of sync
        currentSyncPhase = 1;
        Serial.println("[MQTT] Sync Phase 1 started (recent 20 messages) - disabling status updates");
    }
    
    JsonArray msgArray = doc["messages"];
    int msgCount = 0;
    
    // Normalize our MAC for comparison
    String myMacStr = String(myMAC, HEX);
    myMacStr.toLowerCase();
    
    for (JsonObject msgObj : msgArray) {
        Message msg;
        msg.sender = msgObj["sender"] | "";
        msg.senderMAC = msgObj["senderMAC"] | "";
        msg.content = msgObj["content"] | "";
        msg.timestamp = msgObj["timestamp"] | 0;
        msg.messageId = msgObj["messageId"] | "";
        msg.received = msgObj["received"] | true;
        msg.status = (MessageStatus)(msgObj["status"] | MSG_RECEIVED);
        msg.villageId = msgObj["villageId"] | "";  // Extract village ID from sync response
        
        // Store sender MAC from first message for background sync continuation
        if (msgCount == 0 && batch == 1 && phase == 1 && !msg.senderMAC.isEmpty()) {
            syncTargetMAC = msg.senderMAC;
            Serial.println("[MQTT] Stored sync target MAC: " + syncTargetMAC + " for background phases");
        }
        
        // NOTE: We do NOT check seenMessageIds for sync messages!
        // Sync responses represent "messages in the other device's storage"
        // We WANT to receive our own sent messages back - that confirms they were stored
        // Deduplication is handled by Village::saveMessage() which checks storage
        
        // CRITICAL: Send ACK for synced messages that are NOT ours
        // This ensures the sender gets delivery confirmation even if recipient was offline
        if (msg.senderMAC != myMacStr && !msg.messageId.isEmpty()) {
            Serial.println("[MQTT] Sending ACK for synced message: " + msg.messageId);
            sendAck(msg.messageId, msg.senderMAC, msg.villageId);
        }
        
        // Deliver to app via message callback (deduplication happens in Village::saveMessage)
        if (onMessageReceived) {
            Serial.println("[MQTT] Synced message: " + msg.messageId + " from " + msg.sender + " content='" + msg.content + "'");
            onMessageReceived(msg);
            msgCount++;
        }
    }
    
    // End of phase
    if (batch == total) {
        Serial.println("[MQTT] Phase " + String(phase) + " complete - processed " + String(msgCount) + " messages");
        
        // ===== SYNC DEBUG: Trigger message store dump after phase completes =====
        extern void dumpMessageStoreDebug(int completedPhase);
        dumpMessageStoreDebug(phase);
        
        if (phase == 1) {
            // Phase 1 complete - re-enable status updates, user has recent messages
            isSyncing = false;
            Serial.println("[MQTT] Phase 1 complete - recent messages synced, re-enabled status updates");
            logger.info("Phase 1 complete: " + String(msgCount) + " recent msgs");
            
            // Notify about boot sync completion (only first time after boot)
            static bool bootSyncNotified = false;
            if (!bootSyncNotified && onBootSyncComplete != nullptr) {
                onBootSyncComplete();
                bootSyncNotified = true;
                Serial.println("[MQTT] Boot sync complete callback fired");
            }
            
            // Store sync state for background phase continuation
            if (morePhases) {
                currentSyncPhase = 2;
                lastSyncPhaseTime = millis();
                Serial.println("[MQTT] More history available - will request Phase 2 in background after delay");
            } else {
                currentSyncPhase = 0;  // All done
                Serial.println("[MQTT] Sync fully complete - no more history");
            }
        } else {
            // Background phase complete
            Serial.println("[MQTT] Background phase " + String(phase) + " complete");
            logger.info("Phase " + String(phase) + " complete: " + String(msgCount) + " msgs");
            
            if (morePhases) {
                currentSyncPhase = phase + 1;
                lastSyncPhaseTime = millis();
                Serial.println("[MQTT] Will request Phase " + String(currentSyncPhase) + " in background");
            } else {
                currentSyncPhase = 0;  // All history synced
                Serial.println("[MQTT] All history synced");
            }
        }
    }
    
    Serial.println("[MQTT] Processed " + String(msgCount) + " synced messages in phase " + String(phase));
    logger.info("Synced " + String(msgCount) + " messages");
}

String MQTTMessenger::getConnectionStatus() {
    if (WiFi.status() != WL_CONNECTED) {
        return "No WiFi";
    }
    
    if (!connected || !mqttClient) {
        return "Disconnected";
    }
    
    return "Connected";
}

// Multi-village subscription management

void MQTTMessenger::addVillageSubscription(const String& villageId, const String& villageName, const String& username, const uint8_t* encKey) {
    // Check if already subscribed - UPDATE username if it exists
    for (auto& village : subscribedVillages) {
        if (village.villageId == villageId) {
            // Update username and encryption key
            village.username = username;
            memcpy(village.encryptionKey, encKey, 32);
            Serial.println("[MQTT] Updated village subscription: " + villageName + " (username: " + username + ")");
            
            // If this is the active village, update currentUsername too
            if (currentVillageId == villageId) {
                currentUsername = username;
                Serial.println("[MQTT] Updated active village username to: " + username);
            }
            return;
        }
    }
    
    // Add new subscription
    VillageSubscription sub;
    sub.villageId = villageId;
    sub.villageName = villageName;
    sub.username = username;
    memcpy(sub.encryptionKey, encKey, 32);
    subscribedVillages.push_back(sub);
    
    Serial.println("[MQTT] Added village subscription: " + villageName + " (" + villageId + ")");
    
    // Subscribe to MQTT topic if already connected
    if (isConnected()) {
        String baseTopic = "smoltxt/" + villageId + "/#";
        esp_mqtt_client_subscribe(mqttClient, baseTopic.c_str(), 1);  // QoS 1
        Serial.println("[MQTT] Subscribed to topic: " + baseTopic);
    }
}

void MQTTMessenger::removeVillageSubscription(const String& villageId) {
    for (auto it = subscribedVillages.begin(); it != subscribedVillages.end(); ++it) {
        if (it->villageId == villageId) {
            Serial.println("[MQTT] Removing village subscription: " + it->villageName);
            
            // Unsubscribe from MQTT topic if connected
            if (isConnected()) {
                String baseTopic = "smoltxt/" + villageId + "/#";
                esp_mqtt_client_unsubscribe(mqttClient, baseTopic.c_str());
                Serial.println("[MQTT] Unsubscribed from topic: " + baseTopic);
            }
            
            subscribedVillages.erase(it);
            return;
        }
    }
}

void MQTTMessenger::setActiveVillage(const String& villageId) {
    VillageSubscription* village = findVillageSubscription(villageId);
    if (village) {
        currentVillageId = village->villageId;
        currentVillageName = village->villageName;
        currentUsername = village->username;
        
        // Update encryption key for sending
        if (encryption) {
            encryption->setKey(village->encryptionKey);
        }
        
        Serial.println("[MQTT] Active village set to: " + currentVillageName);
    } else {
        Serial.println("[MQTT] Warning: Village not found: " + villageId);
    }
}

void MQTTMessenger::subscribeToAllVillages() {
    Serial.println("[MQTT] Scanning for saved villages...");
    
    // Clear existing subscriptions
    subscribedVillages.clear();
    
    // Scan all village slots (0-9)
    for (int slot = 0; slot < 10; slot++) {
        if (Village::hasVillageInSlot(slot)) {
            Village tempVillage;
            if (tempVillage.loadFromSlot(slot)) {
                addVillageSubscription(
                    tempVillage.getVillageId(),
                    tempVillage.getVillageName(),
                    tempVillage.getUsername(),
                    tempVillage.getEncryptionKey()
                );
            }
        }
    }
    
    Serial.println("[MQTT] Subscribed to " + String(subscribedVillages.size()) + " villages");
    logger.info("MQTT: Subscribed to " + String(subscribedVillages.size()) + " villages");
}

VillageSubscription* MQTTMessenger::findVillageSubscription(const String& villageId) {
    for (auto& village : subscribedVillages) {
        if (village.villageId == villageId) {
            return &village;
        }
    }
    return nullptr;
}

// ============ Invite Code Protocol ============

bool MQTTMessenger::publishInvite(const String& inviteCode, const String& villageId, const String& villageName, const uint8_t* encryptionKey, int conversationType) {
    if (!connected || !mqttClient) {
        Serial.println("[MQTT] Cannot publish invite - not connected");
        logger.error("Invite publish failed: not connected");
        return false;
    }
    
    // Create JSON payload with village info
    JsonDocument doc;
    doc["villageId"] = villageId;
    doc["villageName"] = villageName;
    doc["timestamp"] = millis();
    
    // Base64 encode the encryption key
    char encodedKey[64];
    size_t encodedLen = 0;
    mbedtls_base64_encode((unsigned char*)encodedKey, sizeof(encodedKey), &encodedLen, encryptionKey, 32);
    encodedKey[encodedLen] = '\0';
    doc["key"] = String(encodedKey);
    doc["type"] = conversationType;  // Add conversation type from parameter
    doc["creatorUsername"] = currentUsername;  // Include creator's username
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.println("[MQTT] ====== PUBLISHING INVITE ======");
    Serial.println("[MQTT] Publishing invite to code: " + inviteCode);
    Serial.println("[MQTT] Invite payload: " + payload);
    Serial.println("[MQTT] Payload length: " + String(payload.length()));
    
    // Publish unencrypted to invite topic (the invite code itself is the secret)
    String topic = "smoltxt/invites/" + inviteCode;
    Serial.println("[MQTT] Topic: " + topic);
    Serial.println("[MQTT] QoS: 1, Retain: 1");
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), payload.c_str(), 
                                        payload.length(), 1, 1);  // QoS 1, retain=1
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] Invite published successfully (msg_id=" + String(msg_id) + ")");
        Serial.println("[MQTT] ==================================");
        logger.info("Invite published: code=" + inviteCode);
        
        // Wait a moment to ensure message is transmitted before any disconnect
        delay(100);
        
        return true;
    } else {
        Serial.println("[MQTT] Invite publish FAILED (msg_id=" + String(msg_id) + ")");
        Serial.println("[MQTT] ==================================");
        logger.error("Invite publish failed");
        return false;
    }
}

bool MQTTMessenger::unpublishInvite(const String& inviteCode) {
    if (!connected || !mqttClient) {
        Serial.println("[MQTT] Cannot unpublish invite - not connected");
        return false;
    }
    
    // Publish empty retained message to clear it
    String topic = "smoltxt/invites/" + inviteCode;
    int msg_id = esp_mqtt_client_publish(mqttClient, topic.c_str(), "", 0, 1, 1);  // QoS 1, retain=1, empty payload
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] Invite unpublished (cleared): " + inviteCode);
        logger.info("Invite unpublished: code=" + inviteCode);
        return true;
    } else {
        Serial.println("[MQTT] Invite unpublish failed");
        return false;
    }
}

bool MQTTMessenger::subscribeToInvite(const String& inviteCode) {
    if (!connected || !mqttClient) {
        Serial.println("[MQTT] Cannot subscribe to invite - not connected");
        Serial.println("[MQTT] connected=" + String(connected) + " mqttClient=" + String((mqttClient != nullptr) ? "yes" : "no"));
        return false;
    }
    
    String topic = "smoltxt/invites/" + inviteCode;
    Serial.println("[MQTT] Subscribing to topic: " + topic);
    int msg_id = esp_mqtt_client_subscribe(mqttClient, topic.c_str(), 1);
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] Subscribed to invite: " + inviteCode + " (msg_id=" + String(msg_id) + ")");
        logger.info("Subscribed to invite: " + inviteCode);
        return true;
    } else {
        Serial.println("[MQTT] Failed to subscribe to invite (msg_id=" + String(msg_id) + ")");
        logger.error("Invite subscribe failed");
        return false;
    }
}

bool MQTTMessenger::unsubscribeFromInvite(const String& inviteCode) {
    if (!mqttClient) {
        return false;
    }
    
    String topic = "smoltxt/invites/" + inviteCode;
    int msg_id = esp_mqtt_client_unsubscribe(mqttClient, topic.c_str());
    
    if (msg_id >= 0) {
        Serial.println("[MQTT] Unsubscribed from invite: " + inviteCode);
        logger.info("Unsubscribed from invite: " + inviteCode);
        return true;
    } else {
        Serial.println("[MQTT] Failed to unsubscribe from invite");
        return false;
    }
}

