#include "Village.h"
#include "Logger.h"
#include <Crypto.h>
#include <SHA256.h>
#include <RNG.h>
#include <algorithm>  // For std::sort

Village::Village() {
    initialized = false;
    isOwner = false;
    memset(villageId, 0, 37);
    memset(villageName, 0, MAX_VILLAGE_NAME);
    memset(myUsername, 0, MAX_USERNAME);
    memset(encryptionKey, 0, KEY_SIZE);
    members.clear();

// ...existing code...
}

// Stub implementation for saveToSlot
bool Village::saveToSlot(int slot) {
    // TODO: Implement saving logic
    return true;
}

// Stub implementation for loadFromSlot
bool Village::loadFromSlot(int slot) {
    // TODO: Implement loading logic
    return true;
}

String Village::hashPassword(const String& password) {
    SHA256 sha256;
    uint8_t hash[32];
    
    sha256.reset();
    sha256.update((const uint8_t*)password.c_str(), password.length());
    sha256.finalize(hash, 32);
    
    // Convert to hex string
    String hashStr = "";
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        hashStr += hex;
    }
    return hashStr;
}



String Village::generateRandomUUID() {
    // Generate random UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    RNG.begin("SmolTxt");
    uint8_t random[16];
    RNG.rand(random, 16);
    
    // Set version (4) and variant bits
    random[6] = (random[6] & 0x0F) | 0x40;  // Version 4
    random[8] = (random[8] & 0x3F) | 0x80;  // Variant 10
    
    char uuid[37];
    snprintf(uuid, sizeof(uuid),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        random[0], random[1], random[2], random[3],
        random[4], random[5], random[6], random[7],
        random[8], random[9], random[10], random[11],
        random[12], random[13], random[14], random[15]);
    
    return String(uuid);
}

void Village::generateRandomEncryptionKey() {
    // Generate pure random 256-bit encryption key
    RNG.begin("SmolTxt");
    RNG.rand(encryptionKey, KEY_SIZE);
}



bool Village::createVillage(const String& name) {
    if (name.length() == 0 || name.length() >= MAX_VILLAGE_NAME) {
        logger.error("Village create failed: invalid name length");
        return false;
    }
    
    // Generate random village ID
    String uuid = generateRandomUUID();
    strncpy(villageId, uuid.c_str(), 36);
    villageId[36] = '\0';
    
    logger.info("Village created: " + name + " (ID: " + String(villageId) + ")");
    Serial.println("[Village] Created with ID: " + String(villageId));
    
    strncpy(villageName, name.c_str(), MAX_VILLAGE_NAME - 1);
    villageName[MAX_VILLAGE_NAME - 1] = '\0';
    
    // No password needed anymore
    memset(villagePassword, 0, MAX_PASSWORD);
    
    // Generate random encryption key
    generateRandomEncryptionKey();
    
    // Debug: Print encryption key
    Serial.print("[Village] Encryption key: ");
    for (int i = 0; i < 16; i++) {
        Serial.print(encryptionKey[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    
    isOwner = true;
    initialized = true;
    members.clear();
    rebuildMessageIdCache();  // Build cache to prevent duplicate messages

    
    return true;  // Don't save here - main.cpp will save to correct slot
}



bool Village::joinVillage(const String& username, const String& password) {
    // For joining, we need to load existing village data
    // This will be set when the owner adds us and we authenticate
    if (!loadFromFile()) {
        return false;
    }
    
    return authenticateMember(username, password);
}

bool Village::addMember(const String& username, const String& password) {
    if (!isOwner || !initialized) {
        return false;
    }
    
    if (username.length() == 0 || username.length() >= MAX_USERNAME) {
        return false;
    }
    
    if (members.size() >= MAX_MEMBERS) {
        return false;
    }
    
    // Check if member already exists
    for (const auto& member : members) {
        if (strcmp(member.username, username.c_str()) == 0) {
            return false;  // Already exists
        }
    }
    
    Member newMember;
    strncpy(newMember.username, username.c_str(), MAX_USERNAME - 1);
    newMember.username[MAX_USERNAME - 1] = '\0';
    
    String hash = hashPassword(password);
    strncpy(newMember.passwordHash, hash.c_str(), 64);
    newMember.passwordHash[64] = '\0';
    
    newMember.active = true;
    
    members.push_back(newMember);
    
    return saveToFile();
}

bool Village::removeMember(const String& username) {
    if (!isOwner || !initialized) {
        return false;
    }
    
    for (auto it = members.begin(); it != members.end(); ++it) {
        if (strcmp(it->username, username.c_str()) == 0) {
            members.erase(it);
            return saveToFile();
        }
    }
    
    return false;
}

std::vector<String> Village::getMemberList() {
    std::vector<String> list;
    for (const auto& member : members) {
        if (member.active) {
            list.push_back(String(member.username));
        }
    }
    return list;
}

bool Village::authenticateMember(const String& username, const String& password) {
    String hash = hashPassword(password);
    
    for (const auto& member : members) {
        if (strcmp(member.username, username.c_str()) == 0 &&
            strcmp(member.passwordHash, hash.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

bool Village::saveToFile() {
    return saveToSlot(0);  // Default to slot 0
}

bool Village::loadFromFile() {
    return loadFromSlot(0);  // Default to slot 0
}

std::vector<String> Village::listVillages() {
    std::vector<String> villages;
    
    if (!LittleFS.begin(true)) {
        return villages;
    }
    
    for (int i = 0; i < 10; i++) {
        String name = getVillageNameFromSlot(i);
        if (name.length() > 0) {
            villages.push_back(String(i) + ": " + name);
        }
    }
    
    return villages;
}

bool Village::hasVillageInSlot(int slot) {
    if (slot < 0 || slot > 9) return false;
    
    if (!LittleFS.begin(true)) return false;
    
    String filename = "/village_" + String(slot) + ".dat";
    if (!LittleFS.exists(filename)) return false;
    
    // CRITICAL: Verify file is actually readable and valid JSON
    // This prevents menu misalignment when files are corrupted
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.println("[Village] WARNING: File exists but cannot be opened: " + filename);
        return false;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("[Village] WARNING: File corrupted, JSON parse failed: " + filename);
        Serial.println("[Village] Parse error: " + String(error.c_str()));
        return false;
    }
    
    // Verify critical fields exist
    if (!doc.containsKey("villageId") || !doc.containsKey("villageName")) {
        Serial.println("[Village] WARNING: File missing critical fields: " + filename);
        return false;
    }
    
    return true;
}

String Village::getVillageNameFromSlot(int slot) {
    if (slot < 0 || slot > 9) return "";
    
    if (!LittleFS.begin(true)) return "";
    
    String filename = "/village_" + String(slot) + ".dat";
    if (!LittleFS.exists(filename)) return "";
    
    File file = LittleFS.open(filename, "r");
    if (!file) return "";
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) return "";
    
    return doc["villageName"].as<String>();
}

String Village::getVillageIdFromSlot(int slot) {
    if (slot < 0 || slot > 9) return "";
    
    if (!LittleFS.begin(true)) return "";
    
    String filename = "/village_" + String(slot) + ".dat";
    if (!LittleFS.exists(filename)) return "";
    
    File file = LittleFS.open(filename, "r");
    if (!file) return "";
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) return "";
    
    return doc["villageId"].as<String>();
}

int Village::findVillageSlotById(const String& villageId) {
    if (!LittleFS.begin(true)) return -1;
    
    // Check each slot for matching village ID
    for (int slot = 0; slot < 10; slot++) {
        String filename = "/village_" + String(slot) + ".dat";
        if (!LittleFS.exists(filename)) continue;
        
        File file = LittleFS.open(filename, "r");
        if (!file) continue;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        
        if (error) continue;
        
        String slotVillageId = doc["villageId"].as<String>();
        if (slotVillageId == villageId) {
            Serial.println("[Village] Found existing village with ID '" + villageId + "' in slot " + String(slot));
            return slot;
        }
    }
    
    Serial.println("[Village] No existing village found with ID: " + villageId);
    return -1;  // Not found
}

void Village::deleteSlot(int slot) {
    if (slot < 0 || slot > 9) return;
    
    if (!LittleFS.begin(true)) return;
    
    String filename = "/village_" + String(slot) + ".dat";
    LittleFS.remove(filename);
    
    String msgFilename = "/messages_" + String(slot) + ".dat";
    LittleFS.remove(msgFilename);
}

void Village::clearVillage() {
    initialized = false;
    isOwner = false;
    memset(villageName, 0, MAX_VILLAGE_NAME);
    memset(myUsername, 0, MAX_USERNAME);
    memset(encryptionKey, 0, KEY_SIZE);
    members.clear();
}

void Village::setUsername(const String& username) {
    if (username.length() > 0 && username.length() < MAX_USERNAME) {
        strncpy(myUsername, username.c_str(), MAX_USERNAME - 1);
        myUsername[MAX_USERNAME - 1] = '\0';
    }
}

void Village::setVillageName(const String& name) {
    if (name.length() > 0 && name.length() < MAX_VILLAGE_NAME) {
        strncpy(villageName, name.c_str(), MAX_VILLAGE_NAME - 1);
        villageName[MAX_VILLAGE_NAME - 1] = '\0';
        Serial.println("[Village] Name updated to: " + name);
    }
}

bool Village::saveMessage(const Message& msg) {
    if (!initialized) {
        logger.error("Save message failed: village not initialized");
        return false;
    }
    
    // Check for duplicate message ID (skip empty IDs from old messages)
    if (!msg.messageId.isEmpty() && messageIdExists(msg.messageId)) {
        logger.info("Duplicate message skipped: id=" + msg.messageId);
        return true;  // Return true as it's not an error, message already exists
    }
    
    File file = LittleFS.open("/messages.dat", "a");
    if (!file) {
        logger.critical("Failed to open messages.dat for writing");
        Serial.println("[Village] Failed to open messages file");
        return false;
    }
    
    JsonDocument doc;
    doc["village"] = villageId;  // Use UUID for stable filtering
    doc["sender"] = msg.sender;
    doc["senderMAC"] = msg.senderMAC;
    doc["content"] = msg.content;
    doc["timestamp"] = msg.timestamp;
    doc["received"] = msg.received;
    doc["status"] = (int)msg.status;
    doc["messageId"] = msg.messageId;
    
    serializeJson(doc, file);
    file.println();
    file.flush();  // CRITICAL: Ensure data is written to disk before closing
    file.close();
    
    // Add to cache for deduplication
    if (!msg.messageId.isEmpty()) {
        messageIdCache.insert(msg.messageId);
    }
    
    logger.info("Message saved: id=" + msg.messageId + " from=" + msg.sender + " village=" + String(villageId));
    return true;
}

bool Village::saveMessageToFile(const Message& msg) {
    // Static method to save a message without needing an initialized village
    // Used for saving messages to non-active villages
    
    if (msg.villageId.isEmpty()) {
        Serial.println("[Village] Cannot save message without villageId");
        return false;
    }
    
    File file = LittleFS.open("/messages.dat", "a");
    if (!file) {
        Serial.println("[Village] Failed to open messages.dat for writing");
        return false;
    }
    
    JsonDocument doc;
    doc["village"] = msg.villageId;
    doc["sender"] = msg.sender;
    doc["senderMAC"] = msg.senderMAC;
    doc["content"] = msg.content;
    doc["timestamp"] = msg.timestamp;
    doc["received"] = msg.received;
    doc["status"] = (int)msg.status;
    doc["messageId"] = msg.messageId;
    
    String json;
    serializeJson(doc, json);
    file.println(json);
    file.flush();  // CRITICAL: Ensure data is written to disk before closing
    file.close();
    
    Serial.println("[Village] Message saved to file: id=" + msg.messageId + " village=" + msg.villageId);
    return true;
}

std::vector<Message> Village::loadMessages() {
    std::vector<Message> messages;
    
    if (!initialized) {
        logger.error("Load messages failed: village not initialized");
        return messages;
    }
    
    File file = LittleFS.open("/messages.dat", "r");
    if (!file) {
        logger.info("No messages.dat file found (new village)");
        return messages;
    }
    
    String currentVillageId = String(villageId);  // Use UUID for stable filtering
    logger.info("Loading messages for village: " + currentVillageId);
    
    int totalLines = 0;
    int emptyLines = 0;
    int parseErrors = 0;
    int wrongVillage = 0;
    int matched = 0;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        totalLines++;
        
        if (line.length() == 0) {
            emptyLines++;
            continue;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) {
            logger.error("JSON parse error in messages.dat line " + String(totalLines));
            parseErrors++;
            continue;
        }
        
        String msgVillage = doc["village"] | "";
        
        // SECURITY: Discard messages without villageId (malformed/corrupted data)
        // Never assume which village they belong to - could mix private/group messages
        if (msgVillage.isEmpty() || msgVillage != currentVillageId) {
            wrongVillage++;
            continue;  // Filter by UUID
        }
        
        matched++;
        Message msg;
        msg.sender = doc["sender"] | "";
        msg.senderMAC = doc["senderMAC"] | "";
        msg.content = doc["content"] | "";
        msg.timestamp = doc["timestamp"] | 0;
        msg.received = doc["received"] | false;
        msg.status = (MessageStatus)(doc["status"] | MSG_SENT);
        msg.messageId = doc["messageId"] | "";
        msg.villageId = msgVillage;  // Populate villageId from loaded data
        
        messages.push_back(msg);
    }
    
    file.close();
    
    logger.info("File stats: total=" + String(totalLines) + " empty=" + String(emptyLines) + 
                " parseErr=" + String(parseErrors) + " wrongUUID=" + String(wrongVillage) + 
                " matched=" + String(matched));
    logger.info("Loaded " + String(messages.size()) + " messages for village " + currentVillageId);
    
    // Sort messages by timestamp to ensure chronological order
    std::sort(messages.begin(), messages.end(), [](const Message& a, const Message& b) {
        return a.timestamp < b.timestamp;
    });
    
    Serial.println("[Village] Loaded " + String(messages.size()) + " messages (sorted by timestamp)");
    return messages;
}

bool Village::clearMessages() {
    if (!initialized) return false;
    
    if (LittleFS.remove("/messages.dat")) {
        Serial.println("[Village] Messages cleared");
        return true;
    }
    Serial.println("[Village] Failed to clear messages");
    return false;
}







bool Village::messageIdExists(const String& messageId) {
    if (messageId.isEmpty()) return false;
    return messageIdCache.find(messageId) != messageIdCache.end();
}

void Village::rebuildMessageIdCache() {
    messageIdCache.clear();
    
    if (!initialized) return;
    
    File file = LittleFS.open("/messages.dat", "r");
    if (!file) return;
    
    String currentVillageId = String(villageId);
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) continue;
        
        String msgVillage = doc["village"] | "";
        if (msgVillage != currentVillageId) continue;
        
        String messageId = doc["messageId"] | "";
        if (!messageId.isEmpty()) {
            messageIdCache.insert(messageId);
        }
    }
    
    file.close();
    logger.info("Rebuilt message ID cache: " + String(messageIdCache.size()) + " messages");
}
