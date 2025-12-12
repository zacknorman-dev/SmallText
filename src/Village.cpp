#include "Village.h"
#include "Logger.h"
#include <Crypto.h>
#include <SHA256.h>
#include <RNG.h>
#include <algorithm>  // For std::sort

// Word list for passphrase generation (easy to type and remember)
const char* PASSPHRASE_WORDS[] = {
    "apple", "blue", "cat", "dog", "east", "fire", "green", "happy",
    "ice", "jump", "king", "lion", "moon", "north", "ocean", "pink",
    "quick", "red", "sun", "tree", "up", "violet", "west", "yellow",
    "zero", "bear", "cloud", "dragon", "earth", "frost", "gold", "hero",
    "island", "jade", "knight", "lake", "magic", "night", "orange", "pearl",
    "quest", "river", "star", "tiger", "ultra", "vine", "wind", "zebra"
};
const int PASSPHRASE_WORD_COUNT = 48;

Village::Village() {
    initialized = false;
    isOwner = false;
    memset(villageId, 0, 37);
    memset(villageName, 0, MAX_VILLAGE_NAME);
    memset(myUsername, 0, MAX_USERNAME);
    memset(encryptionKey, 0, KEY_SIZE);
    members.clear();
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

void Village::generateEncryptionKey() {
    // Generate random 256-bit key
    RNG.begin("SmolTxt");
    RNG.rand(encryptionKey, KEY_SIZE);
}

String generateUUID() {
    // Generate UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    // where x is random hex digit, y is 8/9/a/b
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

String Village::deriveVillageIdFromPassword(const String& password) {
    // Derive deterministic UUID from password - both creator and joiner will get same ID
    SHA256 sha256;
    uint8_t hash[32];
    
    sha256.reset();
    sha256.update((const uint8_t*)password.c_str(), password.length());
    sha256.update((const uint8_t*)"VillageID", 9);  // Salt to make it different from encryption key
    sha256.finalize(hash, 32);
    
    // Use first 16 bytes of hash as UUID
    char uuid[37];
    snprintf(uuid, sizeof(uuid),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        hash[0], hash[1], hash[2], hash[3],
        hash[4], hash[5], hash[6], hash[7],
        hash[8], hash[9], hash[10], hash[11],
        hash[12], hash[13], hash[14], hash[15]);
    
    return String(uuid);
}

void Village::deriveKeyFromPassword(const String& password) {
    // Use PBKDF2-like key derivation with SHA256
    // Simple version: hash password + salt multiple times
    SHA256 sha256;
    uint8_t hash[32];
    
    // First hash: password + "SmolTxt" as salt
    sha256.reset();
    sha256.update((const uint8_t*)password.c_str(), password.length());
    sha256.update((const uint8_t*)"SmolTxt", 7);
    sha256.finalize(hash, 32);
    
    // Multiple rounds for key strengthening (1000 iterations)
    for (int i = 0; i < 1000; i++) {
        sha256.reset();
        sha256.update(hash, 32);
        sha256.finalize(hash, 32);
    }
    
    // Use final hash as encryption key
    memcpy(encryptionKey, hash, KEY_SIZE);
}

String Village::generatePassphrase() {
    // Generate random 2-word passphrase like "green dragon"
    RNG.begin("SmolTxt");
    
    uint8_t random[2];
    RNG.rand(random, 2);
    
    int word1Index = random[0] % PASSPHRASE_WORD_COUNT;
    int word2Index = random[1] % PASSPHRASE_WORD_COUNT;
    
    String word1 = String(PASSPHRASE_WORDS[word1Index]);
    String word2 = String(PASSPHRASE_WORDS[word2Index]);
    
    // Lowercase with space (case insensitive)
    word1.toLowerCase();
    word2.toLowerCase();
    
    return word1 + " " + word2;
}

String Village::deriveVillageNameFromPassword(const String& password) {
    // Derive a deterministic but human-readable village name from password
    // Use hash to pick 2 words from the word list
    SHA256 sha256;
    uint8_t hash[32];
    
    sha256.reset();
    sha256.update((const uint8_t*)password.c_str(), password.length());
    sha256.update((const uint8_t*)"VillageName", 11);  // Different salt than UUID
    sha256.finalize(hash, 32);
    
    // Use first 2 bytes to pick words
    int word1Index = hash[0] % PASSPHRASE_WORD_COUNT;
    int word2Index = hash[1] % PASSPHRASE_WORD_COUNT;
    
    String word1 = String(PASSPHRASE_WORDS[word1Index]);
    String word2 = String(PASSPHRASE_WORDS[word2Index]);
    
    // Capitalize first letter of each word
    word1[0] = toupper(word1[0]);
    word2[0] = toupper(word2[0]);
    
    return word1 + " " + word2;  // e.g., "Green Dragon"
}

bool Village::createVillage(const String& name, const String& password) {
    if (name.length() == 0 || name.length() >= MAX_VILLAGE_NAME) {
        logger.error("Village create failed: invalid name length");
        return false;
    }
    
    if (password.length() == 0 || password.length() >= MAX_PASSWORD) {
        logger.error("Village create failed: invalid password length");
        return false;
    }
    
    // Derive deterministic village ID from password (same for all devices with same password)
    String uuid = Village::deriveVillageIdFromPassword(password);
    strncpy(villageId, uuid.c_str(), 36);
    villageId[36] = '\0';
    
    logger.info("Village created: " + name + " (ID: " + String(villageId) + ")");
    Serial.println("[Village] Created with ID: " + String(villageId));
    
    strncpy(villageName, name.c_str(), MAX_VILLAGE_NAME - 1);
    villageName[MAX_VILLAGE_NAME - 1] = '\0';
    
    strncpy(villagePassword, password.c_str(), MAX_PASSWORD - 1);
    villagePassword[MAX_PASSWORD - 1] = '\0';
    
    // Derive encryption key from password
    deriveKeyFromPassword(password);
    
    // Debug: Print encryption key
    Serial.print("[Village] Created with password: ");
    Serial.println(password);
    Serial.print("[Village] Encryption key: ");
    for (int i = 0; i < 16; i++) {
        Serial.print(encryptionKey[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    
    isOwner = true;
    initialized = true;
    members.clear();
    
    return true;  // Don't save here - main.cpp will save to correct slot
}

bool Village::joinVillageAsMember(const String& name, const String& password) {
    // NOTE: When joining with just password, name can be empty - will be received via announcement
    if (name.length() >= MAX_VILLAGE_NAME) {
        logger.error("Village join failed: name too long");
        return false;
    }
    
    if (password.length() == 0 || password.length() >= MAX_PASSWORD) {
        logger.error("Village join failed: invalid password length");
        return false;
    }
    
    // Derive deterministic village ID from password (same as creator)
    String uuid = Village::deriveVillageIdFromPassword(password);
    strncpy(villageId, uuid.c_str(), 36);
    villageId[36] = '\0';
    
    // Use provided name or placeholder if empty (will be updated via announcement)
    if (name.length() > 0) {
        strncpy(villageName, name.c_str(), MAX_VILLAGE_NAME - 1);
    } else {
        strncpy(villageName, "Pending...", MAX_VILLAGE_NAME - 1);
    }
    villageName[MAX_VILLAGE_NAME - 1] = '\0';
    
    logger.info("Village joined: " + String(villageName) + " (ID: " + String(villageId) + ")");
    Serial.println("[Village] Joining with ID: " + String(villageId));
    
    strncpy(villagePassword, password.c_str(), MAX_PASSWORD - 1);
    villagePassword[MAX_PASSWORD - 1] = '\0';
    
    // Derive encryption key from password
    deriveKeyFromPassword(password);
    
    // Debug: Print encryption key
    Serial.print("[Village] Joined with password: ");
    Serial.println(password);
    Serial.print("[Village] Encryption key: ");
    for (int i = 0; i < 16; i++) {
        Serial.print(encryptionKey[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    
    isOwner = false;  // NOT the owner, just a member
    initialized = true;
    members.clear();
    
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
    if (slot < 0 || slot > 9) return false;
    
    if (!LittleFS.begin(true)) {
        Serial.println("Failed to mount LittleFS");
        return false;
    }
    
    String filename = "/village_" + String(slot) + ".dat";
    if (!LittleFS.exists(filename)) {
        return false;
    }
    
    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.println("Failed to open file for reading");
        return false;
    }
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        Serial.println("Failed to parse village data");
        return false;
    }
    
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

bool Village::updateMessageStatus(const String& messageId, int newStatus) {
    if (!initialized) return false;
    
    // Skip empty messageIds (from old builds) - prevents matching all messages
    if (messageId.isEmpty()) {
        logger.error("Skipping status update for empty messageId");
        return false;
    }
    
    // OPTIMIZATION: Only scan last 20 messages for status updates
    // Recent messages are most likely to need status updates (acks, read receipts)
    // This prevents scanning 100+ messages during sync, avoiding watchdog timeouts
    const int MAX_MESSAGES_TO_SCAN = 20;
    
    // CRITICAL FIX: Load ALL messages from file (not filtered by village)
    // to prevent deleting messages from other villages
    std::vector<String> allLines;
    File readFile = LittleFS.open("/messages.dat", "r");
    if (!readFile) {
        logger.error("Failed to open messages.dat for status update");
        return false;
    }
    
    String currentVillageId = String(villageId);
    
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
        
        String msgVillage = doc["village"] | "";
        String msgId = doc["messageId"] | "";
        
        // Count messages from our village first (for scan limit)
        if (msgVillage == currentVillageId) {
            messagesScanned++;
            
            // Stop scanning after MAX_MESSAGES_TO_SCAN from our village
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
    }
    
    if (!found) {
        logger.error("Message not found: " + messageId);
        return false;
    }
    
    // Rewrite the entire file with ALL messages preserved
    File writeFile = LittleFS.open("/messages.dat", "w");
    if (!writeFile) {
        logger.critical("Failed to reopen messages.dat for writing");
        return false;
    }
    
    for (const String& line : allLines) {
        writeFile.println(line);
    }
    
    writeFile.flush();  // CRITICAL: Ensure data is written to disk before closing
    writeFile.close();
    logger.info("Saved " + String(allLines.size()) + " total lines (all villages preserved)");
    return true;
}

bool Village::batchUpdateMessageStatus(const std::vector<String>& messageIds, int newStatus) {
    if (!initialized) return false;
    if (messageIds.empty()) return true;  // Nothing to update
    
    // Create a set for fast lookup
    std::set<String> targetIds(messageIds.begin(), messageIds.end());
    
    // Load ALL messages from file
    std::vector<String> allLines;
    File readFile = LittleFS.open("/messages.dat", "r");
    if (!readFile) {
        logger.error("Failed to open messages.dat for batch status update");
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
    String currentVillageId = String(villageId);
    
    for (int i = 0; i < allLines.size(); i++) {
        String& line = allLines[i];
        
        if (line.length() == 0) continue;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, line);
        
        if (error) continue;
        
        String msgVillage = doc["village"] | "";
        String msgId = doc["messageId"] | "";
        
        // Check if this message is in our batch and from our village
        if (msgVillage == currentVillageId && targetIds.find(msgId) != targetIds.end()) {
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
    File writeFile = LittleFS.open("/messages.dat", "w");
    if (!writeFile) {
        logger.critical("Failed to reopen messages.dat for writing");
        return false;
    }
    
    for (const String& line : allLines) {
        writeFile.println(line);
    }
    
    writeFile.flush();
    writeFile.close();
    logger.info("Saved " + String(allLines.size()) + " total lines (all villages preserved)");
    return true;
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
