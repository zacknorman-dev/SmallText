#include "Village.h"
#include "Logger.h"
#include <Crypto.h>
#include <SHA256.h>
#include <RNG.h>
#include <algorithm>  // For std::sort
#include <map>  // For deduplication tracking

Village::Village() {
    initialized = false;
    isOwner = false;
    memset(villageId, 0, 37);
    memset(villageName, 0, MAX_VILLAGE_NAME);
    memset(myUsername, 0, MAX_USERNAME);
    memset(encryptionKey, 0, KEY_SIZE);
    members.clear();
    conversationType = CONVERSATION_GROUP;  // Default to group
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



bool Village::createVillage(const String& name, ConversationType type) {
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
    
    // Set conversation type
    conversationType = type;
    Serial.println("[Village] Type: " + String(type == CONVERSATION_INDIVIDUAL ? "Individual" : "Group"));
    
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

bool Village::saveToSlot(int slot) {
    if (slot < 0 || slot > 9) return false;
    
    if (!LittleFS.begin(true)) {
        Serial.println("Failed to mount LittleFS");
        return false;
    }
    
    String filename = "/village_" + String(slot) + ".dat";
    File file = LittleFS.open(filename, "w");
    if (!file) {
        Serial.println("Failed to open file for writing");
        return false;
    }
    
    JsonDocument doc;
    doc["villageId"] = villageId;  // Save village ID for cross-device identification
    doc["villageName"] = villageName;
    doc["username"] = myUsername;
    doc["isOwner"] = isOwner;
    doc["initialized"] = initialized;
    doc["conversationType"] = (int)conversationType;  // Save conversation type
    
    // Save encryption key as hex
    String keyHex = "";
    for (int i = 0; i < KEY_SIZE; i++) {
        char hex[3];
        sprintf(hex, "%02x", encryptionKey[i]);
        keyHex += hex;
    }
    doc["key"] = keyHex;
    
    // Save members
    JsonArray membersArray = doc["members"].to<JsonArray>();
    for (const auto& member : members) {
        JsonObject memberObj = membersArray.add<JsonObject>();
        memberObj["username"] = member.username;
        memberObj["passwordHash"] = member.passwordHash;
        memberObj["active"] = member.active;
    }
    
    serializeJson(doc, file);
    file.close();
    
    return true;
}

bool Village::loadFromSlot(int slot) {
    if (slot < 0 || slot > 9) {
        Serial.println("[Village] ERROR: Invalid slot " + String(slot));
        return false;
    }
    
    if (!LittleFS.begin(true)) {
        Serial.println("[Village] ERROR: Failed to mount LittleFS for slot " + String(slot));
        logger.error("Village load failed: LittleFS mount error (slot " + String(slot) + ")");
        return false;
    }
    
    String filename = "/village_" + String(slot) + ".dat";
    if (!LittleFS.exists(filename)) {
        Serial.println("[Village] File does not exist: " + filename);
        return false;
    }
    
    Serial.println("[Village] Loading from " + filename + "...");
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.println("[Village] ERROR: Failed to open " + filename + " for reading");
        logger.error("Village load failed: Cannot open " + filename);
        return false;
    }
    
    size_t fileSize = file.size();
    Serial.println("[Village] File size: " + String(fileSize) + " bytes");
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("[Village] ERROR: Failed to parse JSON in " + filename);
        Serial.println("[Village] Parse error: " + String(error.c_str()));
        logger.error("Village load failed: JSON parse error in slot " + String(slot) + " - " + String(error.c_str()));
        return false;
    }
    
    Serial.println("[Village] JSON parsed successfully");
    
    // Load village ID (may not exist in old files, that's OK)
    if (doc["villageId"].is<const char*>()) {
        strncpy(villageId, doc["villageId"], 36);
        villageId[36] = '\0';
    }
    
    const char* vname = doc["villageName"] | "";
    strncpy(villageName, vname, MAX_VILLAGE_NAME - 1);
    villageName[MAX_VILLAGE_NAME - 1] = '\0';
    
    const char* uname = doc["username"] | "";
    strncpy(myUsername, uname, MAX_USERNAME - 1);
    myUsername[MAX_USERNAME - 1] = '\0';
    isOwner = doc["isOwner"];
    initialized = doc["initialized"];
    
    // Load conversation type (default to GROUP for backwards compatibility)
    conversationType = (ConversationType)(doc["conversationType"] | CONVERSATION_GROUP);
    
    // Load encryption key from hex
    String keyHex = doc["key"].as<String>();
    for (int i = 0; i < KEY_SIZE; i++) {
        char byte[3] = {keyHex[i*2], keyHex[i*2+1], '\0'};
        encryptionKey[i] = strtol(byte, NULL, 16);
    }
    
    // Load members
    members.clear();
    JsonArray membersArray = doc["members"];
    for (JsonObject memberObj : membersArray) {
        Member member;
        strncpy(member.username, memberObj["username"], MAX_USERNAME - 1);
        strncpy(member.passwordHash, memberObj["passwordHash"], 64);
        member.active = memberObj["active"];
        members.push_back(member);
    }
    
    // Rebuild message ID cache to prevent duplicates
    if (initialized) {
        rebuildMessageIdCache();
    }
    
    return true;
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
    if (!doc["villageId"].is<String>() || !doc["villageName"].is<String>()) {
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
    
    // Check if message already exists with matching sender
    // This prevents true duplicates while allowing sync updates
    if (!msg.messageId.isEmpty()) {
        String filename = "/messages_" + String(villageId) + ".dat";
        File checkFile = LittleFS.open(filename, "r");
        if (checkFile) {
            while (checkFile.available()) {
                String line = checkFile.readStringUntil('\n');
                line.trim();
                if (line.isEmpty()) continue;
                
                JsonDocument checkDoc;
                DeserializationError err = deserializeJson(checkDoc, line);
                if (err) continue;
                
                String existingId = checkDoc["messageId"] | "";
                String existingSender = checkDoc["sender"] | "";
                
                // Only skip if BOTH messageId AND sender match
                // This allows sync to receive messages we sent back (they have our sender)
                // but blocks true transport-level duplicates
                if (existingId == msg.messageId && existingSender == msg.sender) {
                    checkFile.close();
                    logger.info("Duplicate message skipped (exact match): id=" + msg.messageId + " sender=" + msg.sender);
                    messageIdCache.insert(msg.messageId);
                    return false;  // Return false to indicate message was NOT saved (duplicate)
                }
            }
            checkFile.close();
        }
    }
    
    String filename = "/messages_" + String(villageId) + ".dat";
    File file = LittleFS.open(filename, "a");
    if (!file) {
        logger.critical("Failed to open " + filename + " for writing");
        Serial.println("[Village] Failed to open messages file: " + filename);
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
    
    String currentVillageId = String(villageId);
    String filename = "/messages_" + currentVillageId + ".dat";
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        logger.info("No message file found (new village): " + filename);
        return messages;
    }
    
    logger.info("Loading messages for village: " + currentVillageId);
    
    int totalLines = 0;
    int emptyLines = 0;
    int parseErrors = 0;
    
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
            logger.error("JSON parse error in " + filename + " line " + String(totalLines));
            parseErrors++;
            continue;
        }
        
        Message msg;
        msg.sender = doc["sender"] | "";
        msg.senderMAC = doc["senderMAC"] | "";
        msg.content = doc["content"] | "";
        msg.timestamp = doc["timestamp"] | 0;
        msg.received = doc["received"] | false;
        msg.status = (MessageStatus)(doc["status"] | MSG_SENT);
        msg.messageId = doc["messageId"] | "";
        msg.villageId = currentVillageId;  // All messages in this file belong to this village
        
        messages.push_back(msg);
    }
    
    file.close();
    
    logger.info("File stats: total=" + String(totalLines) + " empty=" + String(emptyLines) + 
                " parseErr=" + String(parseErrors) + " matched=" + String(totalLines - emptyLines - parseErrors));
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
    
    String filename = "/messages_" + String(villageId) + ".dat";
    if (LittleFS.remove(filename)) {
        Serial.println("[Village] Messages cleared: " + filename);
        return true;
    }
    Serial.println("[Village] Failed to clear messages: " + filename);
    return false;
}

bool Village::updateMessageStatus(const String& messageId, int newStatus) {
    if (!initialized) return false;
    
    // Skip empty messageIds (from old builds) - prevents matching all messages
    if (messageId.isEmpty()) {
        logger.error("Skipping status update for empty messageId");
        return false;
    }
    
    // OPTIMIZATION: Only scan last 20 messages for status updates
    // Recent messages are most likely to need status updates (acks, read receipts)
    const int MAX_MESSAGES_TO_SCAN = 20;
    
    String currentVillageId = String(villageId);
    String filename = "/messages_" + currentVillageId + ".dat";
    
    std::vector<String> allLines;
    File readFile = LittleFS.open(filename, "r");
    if (!readFile) {
        logger.error("Failed to open " + filename + " for status update");
        return false;
    }
    
    // Read all lines into memory first
    while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        line.trim();
        allLines.push_back(line);
    }
    readFile.close();
    
    // SCAN BACKWARDS from end of file (newest messages are appended at bottom)
    bool found = false;
    int messagesScanned = 0;
    
    for (int i = allLines.size() - 1; i >= 0 && !found; i--) {
        String& line = allLines[i];
        
        if (line.length() == 0) {
            continue;  // Skip empty lines
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) {
            continue;  // Skip unparseable lines
        }
        
        String msgId = doc["messageId"] | "";
        
        messagesScanned++;
        
        // Stop scanning after MAX_MESSAGES_TO_SCAN
        if (messagesScanned > MAX_MESSAGES_TO_SCAN) {
            break;
        }
        
        // Check if this is our target message
        if (msgId == messageId) {
            doc["status"] = newStatus;
            found = true;
            
            // Re-serialize the updated message
            String updatedLine;
            serializeJson(doc, updatedLine);
            allLines[i] = updatedLine;
            
            logger.info("Updated message " + messageId + " to status " + String(newStatus));
        }
    }
    
    if (!found) {
        logger.error("Message not found: " + messageId);
        return false;
    }
    
    // Rewrite the entire file
    File writeFile = LittleFS.open(filename, "w");
    if (!writeFile) {
        logger.critical("Failed to reopen " + filename + " for writing");
        return false;
    }
    
    for (const String& line : allLines) {
        writeFile.println(line);
    }
    
    writeFile.flush();  // CRITICAL: Ensure data is written to disk before closing
    writeFile.close();
    logger.info("Saved " + String(allLines.size()) + " total lines");
    return true;
}

bool Village::updateMessageStatusIfLower(const String& messageId, int newStatus) {
    if (!initialized) return false;
    
    // Skip empty messageIds
    if (messageId.isEmpty()) {
        logger.error("Skipping status update for empty messageId");
        return false;
    }
    
    // Use per-village message file
    String filename = "/messages_" + String(villageId) + ".dat";
    
    // Load the message to check current status
    std::vector<String> allLines;
    File readFile = LittleFS.open(filename, "r");
    if (!readFile) {
        logger.error("Failed to open " + filename + " for status check");
        return false;
    }
    
    // Read all lines from this village's file
    while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        line.trim();
        allLines.push_back(line);
    }
    readFile.close();
    
    // Find message and check current status
    bool found = false;
    int currentStatus = MSG_SENT;
    
    for (int i = allLines.size() - 1; i >= 0 && !found; i--) {
        String& line = allLines[i];
        
        if (line.length() == 0) continue;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) continue;
        
        String msgId = doc["messageId"] | "";
        
        if (msgId == messageId) {
            currentStatus = doc["status"] | MSG_SENT;
            found = true;
            
            // Only update if new status is higher
            if (newStatus > currentStatus) {
                doc["status"] = newStatus;
                String updatedLine;
                serializeJson(doc, updatedLine);
                allLines[i] = updatedLine;
                
                logger.info("Updated message " + messageId + " from status " + String(currentStatus) + " to " + String(newStatus));
            } else {
                logger.info("Skipping status update for " + messageId + " - current status " + String(currentStatus) + " >= new status " + String(newStatus));
                return true;  // Not an error, just no update needed
            }
        }
    }
    
    if (!found) {
        logger.error("Message not found: " + messageId);
        return false;
    }
    
    // Rewrite file only if we made changes
    if (found && newStatus > currentStatus) {
        File writeFile = LittleFS.open(filename, "w");
        if (!writeFile) {
            logger.critical("Failed to reopen " + filename + " for writing");
            return false;
        }
        
        for (const String& line : allLines) {
            writeFile.println(line);
        }
        
        writeFile.flush();
        writeFile.close();
        logger.info("Saved " + String(allLines.size()) + " total lines");
    }
    
    return true;
}

bool Village::batchUpdateMessageStatus(const std::vector<String>& messageIds, int newStatus) {
    if (!initialized) return false;
    if (messageIds.empty()) return true;  // Nothing to update
    
    // Create a set for fast lookup
    std::set<String> targetIds(messageIds.begin(), messageIds.end());
    
    // Load messages from per-village file
    String filename = "/messages_" + String(villageId) + ".dat";
    std::vector<String> allLines;
    File readFile = LittleFS.open(filename, "r");
    if (!readFile) {
        logger.error("Failed to open " + filename + " for batch status update");
        return false;
    }
    
    while (readFile.available()) {
        String line = readFile.readStringUntil('\n');
        line.trim();
        allLines.push_back(line);
    }
    readFile.close();
    
    // Update all matching messages in memory
    int updatedCount = 0;
    
    for (int i = 0; i < allLines.size(); i++) {
        String& line = allLines[i];
        
        if (line.length() == 0) continue;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) continue;
        
        String msgId = doc["messageId"] | "";
        
        // Check if this message is in our batch (no village check needed - we're in a per-village file)
        if (targetIds.find(msgId) != targetIds.end()) {
            doc["status"] = newStatus;
            
            // Re-serialize the updated message
            String updatedLine;
            serializeJson(doc, updatedLine);
            allLines[i] = updatedLine;
            
            updatedCount++;
            
            // If we've found all messages, we can stop
            if (updatedCount >= messageIds.size()) break;
        }
    }
    
    logger.info("Batch updated " + String(updatedCount) + " messages to status " + String(newStatus));
    
    // Write once at the end
    File writeFile = LittleFS.open(filename, "w");
    if (!writeFile) {
        logger.critical("Failed to reopen " + filename + " for writing");
        return false;
    }
    
    for (const String& line : allLines) {
        writeFile.println(line);
    }
    
    writeFile.flush();
    writeFile.close();
    logger.info("Saved " + String(allLines.size()) + " total lines");
    return true;
}

bool Village::messageIdExists(const String& messageId) {
    if (messageId.isEmpty()) return false;
    return messageIdCache.find(messageId) != messageIdCache.end();
}

void Village::rebuildMessageIdCache() {
    messageIdCache.clear();
    
    if (!initialized) return;
    
    String filename = "/messages_" + String(villageId) + ".dat";
    File file = LittleFS.open(filename, "r");
    if (!file) return;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) continue;
        
        String messageId = doc["messageId"] | "";
        if (!messageId.isEmpty()) {
            messageIdCache.insert(messageId);
        }
    }
    
    file.close();
    logger.info("Rebuilt message ID cache: " + String(messageIdCache.size()) + " messages");
}

int Village::deduplicateMessages() {
    // Remove duplicate message IDs from storage
    // Keep the message with highest status (READ > RECEIVED > SENT)
    
    if (!initialized) return 0;
    
    String filename = "/messages_" + String(villageId) + ".dat";
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.println("[Village] Failed to open " + filename + " for deduplication");
        return 0;
    }
    
    // Load all messages, track best version of each message ID
    std::map<String, String> bestMessages;  // messageId -> best line
    std::map<String, int> bestStatus;  // messageId -> highest status
    int duplicatesRemoved = 0;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.isEmpty()) continue;
        
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, line);
        if (err) {
            continue;  // Skip malformed lines
        }
        
        String messageId = doc["messageId"] | "";
        if (messageId.isEmpty()) {
            continue;  // Skip lines without message ID
        }
        
        int status = doc["status"] | 0;
        
        // Check if we've seen this message ID before (no village check needed - per-village file)
        if (bestMessages.find(messageId) != bestMessages.end()) {
            // Duplicate found!
            duplicatesRemoved++;
            Serial.println("[Village] Duplicate found: " + messageId + " (keeping highest status)");
            
            // Keep the one with higher status
            if (status > bestStatus[messageId]) {
                bestMessages[messageId] = line;
                bestStatus[messageId] = status;
            }
            // else: current stored message has higher status, discard this one
        } else {
            // First occurrence of this message
            bestMessages[messageId] = line;
            bestStatus[messageId] = status;
        }
    }
    
    file.close();
    
    if (duplicatesRemoved == 0) {
        Serial.println("[Village] No duplicates found");
        return 0;
    }
    
    Serial.println("[Village] Removed " + String(duplicatesRemoved) + " duplicate messages");
    
    // Write back deduplicated messages
    File writeFile = LittleFS.open(filename, "w");
    if (!writeFile) {
        Serial.println("[Village] Failed to reopen " + filename + " for writing");
        return duplicatesRemoved;  // Still return count even if write fails
    }
    
    // Write all best messages
    for (const auto& pair : bestMessages) {
        writeFile.println(pair.second);
    }
    
    writeFile.flush();
    writeFile.close();
    
    Serial.println("[Village] Deduplication complete - " + String(bestMessages.size()) + " unique messages retained");
    return duplicatesRemoved;
}
