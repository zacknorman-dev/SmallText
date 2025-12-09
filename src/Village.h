#ifndef VILLAGE_H
#define VILLAGE_H

#include <Arduino.h>
#include <vector>
#include <set>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "Messages.h"

#define MAX_VILLAGE_NAME 32
#define MAX_USERNAME 32
#define MAX_PASSWORD 64
#define MAX_MEMBERS 20
#define KEY_SIZE 32  // 256-bit key for ChaCha20

struct Member {
    char username[MAX_USERNAME];
    char passwordHash[65];  // SHA256 hash as hex string
    bool active;
};

class Village {
private:
    char villageId[37];  // UUID: 36 chars + null terminator (e.g., "550e8400-e29b-41d4-a716-446655440000")
    char villageName[MAX_VILLAGE_NAME];
    char myUsername[MAX_USERNAME];  // This device's username
    char villagePassword[MAX_PASSWORD];  // User-friendly password for sharing
    uint8_t encryptionKey[KEY_SIZE];
    std::vector<Member> members;
    bool isOwner;
    bool initialized;
    
    String hashPassword(const String& password);
    void generateEncryptionKey();
    void deriveKeyFromPassword(const String& password);
    
public:
    Village();
    
    // Passphrase generation
    String generatePassphrase();  // Generate random 2-word passphrase
    static String deriveVillageNameFromPassword(const String& password);  // Derive deterministic village name
    
    // Village management
    bool createVillage(const String& name, const String& password);
    bool joinVillageAsMember(const String& name, const String& password);
    bool joinVillage(const String& username, const String& password);
    bool isInitialized() { return initialized; }
    bool amOwner() { return isOwner; }
    
    // User identity
    void setUsername(const String& username);
    
    // Member management (owner only)
    bool addMember(const String& username, const String& password);
    bool removeMember(const String& username);
    std::vector<String> getMemberList();
    
    // Authentication
    bool authenticateMember(const String& username, const String& password);
    
    // Getters
    String getVillageId() { return String(villageId); }
    String getVillageName() { return String(villageName); }
    String getUsername() { return String(myUsername); }
    String getPassword() { return String(villagePassword); }
    
    // Setters
    void setVillageName(const String& name);  // Update village name (for joiners receiving announcement)
    
    // Getters
    const char* getVillageId() const { return villageId; }
    const char* getUsername() const { return myUsername; }
    
    // Encryption
    const uint8_t* getEncryptionKey() { return encryptionKey; }
    String getPasswordString();  // Get password as displayable hex string
    
    // Storage
    bool saveToFile();
    bool loadFromFile();
    void clearVillage();
    
    // Multi-village support
    bool saveToSlot(int slot);  // Save to specific slot (0-9)
    bool loadFromSlot(int slot);  // Load from specific slot
    static std::vector<String> listVillages();  // List all saved villages with their names
    static bool hasVillageInSlot(int slot);  // Check if slot has a village
    static String getVillageNameFromSlot(int slot);  // Get village name without loading
    static String getVillageIdFromSlot(int slot);  // Get village ID without loading
    static String deriveVillageIdFromPassword(const String& password);  // Derive UUID from passphrase
    static int findVillageSlotById(const String& villageId);  // Find slot with matching village ID
    static void deleteSlot(int slot);  // Delete village in slot
    
    // Message persistence
    bool saveMessage(const Message& msg);
    static bool saveMessageToFile(const Message& msg);  // Static method to save without loading village
    std::vector<Message> loadMessages();
    bool clearMessages();  // Clear all stored messages
    bool updateMessageStatus(const String& messageId, int newStatus);  // Update status of existing message
    bool messageIdExists(const String& messageId);  // Check if message already saved
    void rebuildMessageIdCache();  // Rebuild in-memory cache of message IDs
    
    int getMemberCount() { return members.size(); }
    
private:
    std::set<String> messageIdCache;  // In-memory cache of message IDs for deduplication
};

#endif
