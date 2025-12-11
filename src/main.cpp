#include <Arduino.h>
#include <Wire.h>
#include "Village.h"
#include "Encryption.h"
#include "MQTTMessenger.h"
#include "Keyboard.h"
#include "UI.h"
#include "Battery.h"
#include "Logger.h"
#include "WiFiManager.h"
#include "OTAUpdater.h"

#define BUILD_NUMBER "v0.39.0"

// Pin definitions for Heltec Vision Master E290
#define I2C_SDA 39
#define I2C_SCL 38

// E-paper display pins for Vision Master E290
#define EPD_RST 5
#define EPD_DC 4
#define EPD_CS 3
#define EPD_BUSY 6
#define EPD_SCK 2
#define EPD_MOSI 1
#define EPD_MISO -1

// Buzzer pin
#define BUZZER_PIN 9
#define BUZZER_CHANNEL 0

// Global objects
Village village;
Encryption encryption;
MQTTMessenger mqttMessenger;
Keyboard keyboard;
UI ui;
Battery battery;
WiFiManager wifiManager;
OTAUpdater otaUpdater;
// Logger is declared in Logger.cpp

// Application state
enum AppState {
  APP_MAIN_MENU,
  APP_SETTINGS_MENU,
  APP_RINGTONE_SELECT,
  APP_WIFI_SETUP_MENU,
  APP_WIFI_SSID_INPUT,
  APP_WIFI_PASSWORD_INPUT,
  APP_WIFI_CONNECTING,
  APP_WIFI_STATUS,
  APP_OTA_CHECKING,
  APP_OTA_UPDATING,
  APP_VILLAGE_MENU,
  APP_VILLAGE_CREATE,
  APP_VILLAGE_JOIN_NAME,
  APP_VILLAGE_JOIN_PASSWORD,
  APP_PASSWORD_INPUT,
  APP_USERNAME_INPUT,
  APP_VIEW_MEMBERS,
  APP_MESSAGING,
  APP_MESSAGE_COMPOSE
};

AppState appState = APP_MAIN_MENU;
String messageComposingText = "";
String tempVillageName = "";  // Temp storage during village creation
String tempWiFiSSID = "";     // Temp storage during WiFi setup
String tempWiFiPassword = ""; // Temp storage during WiFi setup

// Helper function to delay while still polling keyboard frequently
void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    keyboard.update();  // Poll keyboard every 10ms instead of continuously
    mqttMessenger.loop();  // Process MQTT messages during delays
    yield();  // Let ESP32 handle background tasks
    delay(10);  // Add delay to prevent 880k+ polling spam
  }
}

String tempVillagePassword = "";  // Temp storage during village creation
bool isCreatingVillage = true;  // Flag to track if creating (true) or joining (false) village
bool inMessagingScreen = false;  // Flag to track if we're viewing the messaging screen
unsigned long lastMessagingActivity = 0;  // Timestamp of last activity in messaging
const unsigned long MESSAGING_TIMEOUT = 300000;  // 5 minutes timeout
int currentVillageSlot = -1;  // Track which village slot is currently active (-1 = none)
bool isSyncing = false;  // Flag to track if we're currently syncing (skip status updates during sync)

// Ringtone types
enum RingtoneType {
  RINGTONE_RISING,      // 0
  RINGTONE_FALLING,     // 1
  RINGTONE_FIVE_TONE,   // 2
  RINGTONE_TRIPLE_CHIRP,// 3
  RINGTONE_DOUBLE_BEEP, // 4
  RINGTONE_3000HZ,      // 5
  RINGTONE_2500HZ,      // 6
  RINGTONE_2000HZ,      // 7
  RINGTONE_1500HZ,      // 8
  RINGTONE_1000HZ,      // 9
  RINGTONE_500HZ,       // 10
  RINGTONE_OFF          // 11
};

const char* ringtoneNames[] = {
  "Rising Tone",
  "Falling Tone",
  "Five Tone",
  "Triple Chirp",
  "Double Beep",
  "3000 Hz",
  "2500 Hz",
  "2000 Hz",
  "1500 Hz",
  "1000 Hz",
  "500 Hz",
  "Off"
};

// Ringtone settings
RingtoneType selectedRingtone = RINGTONE_RISING;  // Default to Rising Tone
bool ringtoneEnabled = true;  // Default to on
bool hasUnreadMessages = false;  // Track if there are unread messages to ring for
String lastRingtoneVillageId = "";  // Track which village triggered last ringtone

// Play a specific ringtone
void playRingtoneSound(RingtoneType type) {
  switch(type) {
    case RINGTONE_RISING:
      for(int freq = 800; freq <= 2000; freq += 100) {
        ledcWriteTone(BUZZER_CHANNEL, freq);
        delay(30);
      }
      break;
    case RINGTONE_FALLING:
      for(int freq = 2000; freq >= 800; freq -= 100) {
        ledcWriteTone(BUZZER_CHANNEL, freq);
        delay(30);
      }
      break;
    case RINGTONE_FIVE_TONE:
      ledcWriteTone(BUZZER_CHANNEL, 1900);
      delay(100);
      ledcWriteTone(BUZZER_CHANNEL, 0);
      delay(50);
      ledcWriteTone(BUZZER_CHANNEL, 2000);
      delay(100);
      ledcWriteTone(BUZZER_CHANNEL, 0);
      delay(50);
      ledcWriteTone(BUZZER_CHANNEL, 2100);
      delay(100);
      ledcWriteTone(BUZZER_CHANNEL, 0);
      delay(50);
      ledcWriteTone(BUZZER_CHANNEL, 2000);
      delay(100);
      ledcWriteTone(BUZZER_CHANNEL, 0);
      delay(50);
      ledcWriteTone(BUZZER_CHANNEL, 1900);
      delay(100);
      break;
    case RINGTONE_TRIPLE_CHIRP:
      for(int i = 0; i < 3; i++) {
        ledcWriteTone(BUZZER_CHANNEL, 2500);
        delay(50);
        ledcWriteTone(BUZZER_CHANNEL, 0);
        delay(50);
      }
      break;
    case RINGTONE_DOUBLE_BEEP:
      ledcWriteTone(BUZZER_CHANNEL, 1500);
      delay(100);
      ledcWriteTone(BUZZER_CHANNEL, 0);
      delay(50);
      ledcWriteTone(BUZZER_CHANNEL, 1500);
      delay(100);
      break;
    case RINGTONE_3000HZ:
      ledcWriteTone(BUZZER_CHANNEL, 3000);
      delay(200);
      break;
    case RINGTONE_2500HZ:
      ledcWriteTone(BUZZER_CHANNEL, 2500);
      delay(200);
      break;
    case RINGTONE_2000HZ:
      ledcWriteTone(BUZZER_CHANNEL, 2000);
      delay(200);
      break;
    case RINGTONE_1500HZ:
      ledcWriteTone(BUZZER_CHANNEL, 1500);
      delay(200);
      break;
    case RINGTONE_1000HZ:
      ledcWriteTone(BUZZER_CHANNEL, 1000);
      delay(200);
      break;
    case RINGTONE_500HZ:
      ledcWriteTone(BUZZER_CHANNEL, 500);
      delay(200);
      break;
    case RINGTONE_OFF:
      // No sound
      break;
  }
  ledcWriteTone(BUZZER_CHANNEL, 0);  // Stop
}

// Play the selected ringtone (for notifications)
void playRingtone() {
  if (selectedRingtone == RINGTONE_OFF) return;
  
  Serial.print("[Ringtone] Playing: ");
  Serial.println(ringtoneNames[selectedRingtone]);
  playRingtoneSound(selectedRingtone);
}

// Typing detection - pause non-critical operations while user is actively typing
unsigned long lastKeystroke = 0;
const unsigned long TYPING_TIMEOUT = 1500;  // Consider "typing" if keystroke within last 1.5 seconds
bool isUserTyping() {
  // If no keyboard hardware, never defer updates to prevent freeze
  if (!keyboard.isKeyboardPresent()) {
    return false;
  }
  return (millis() - lastKeystroke) < TYPING_TIMEOUT;
}

// Read receipt queue for async processing
struct ReadReceiptQueueItem {
  String messageId;
  String recipientMAC;
};
std::vector<ReadReceiptQueueItem> readReceiptQueue;
const int MAX_MESSAGES_TO_LOAD = 30;  // Only load most recent 30 messages
const int MAX_READ_RECEIPTS = 10;  // Only send read receipts for last 10 unread messages
unsigned long lastTransmission = 0;  // Track when last transmitted (for timing read receipts)
unsigned long lastOTACheck = 0;  // Track automatic OTA update checks

// Power management - use single key long-press since CardKB can't detect simultaneous keys
unsigned long shutdownHoldStart = 0;
const unsigned long SHUTDOWN_HOLD_TIME = 3000;  // 3 seconds to trigger shutdown
bool isShuttingDown = false;
char lastShutdownKey = 0;

// Get current Unix timestamp (seconds since epoch)
unsigned long getCurrentTime() {
  // Always return Unix timestamp in seconds
  // If WiFi has synced NTP, use accurate real-world time
  long offset = wifiManager.getTimeOffset();
  if (offset != 0) {
    return (millis() / 1000) + offset;
  }
  
  // If NTP not synced yet, estimate based on compile time
  // This ensures we always use Unix epoch seconds, never millis()
  // __DATE__ and __TIME__ give compile timestamp, which is close enough for ordering
  // Typical compile: "Dec  9 2025" and "14:30:00"
  // We use a rough approximation: assume we're within a day of compile time
  // Real fix happens when NTP syncs in a few seconds after WiFi connects
  
  // December 9, 2025 00:00:00 UTC ≈ 1765324800 seconds since epoch
  // This is just a reasonable starting point until NTP syncs
  return 1765324800 + (millis() / 1000);
}

// Power management - graceful shutdown
void enterDeepSleep() {
  Serial.println("[Power] Entering deep sleep mode");
  logger.info("Entering deep sleep");
  
  // Allow MQTT to flush any pending messages
  if (mqttMessenger.isConnected()) {
    for (int i = 0; i < 10; i++) {
      mqttMessenger.loop();
      smartDelay(100);
    }
    Serial.println("[Power] MQTT messages flushed");
  }
  
  // Show powering down message
  ui.showPoweringDown();
  smartDelay(1000);
  
  // Show sleep screen
  ui.showSleepScreen();
  smartDelay(1000);
  
  // Configure wake - just stay asleep until manual reset/power cycle
  // User will press reset button or power cycle when they want to wake up
  Serial.println("[Power] Entering deep sleep - press reset button to wake");
  
  Serial.println("[Power] Entering deep sleep now");
  Serial.flush();
  
  // Enter deep sleep with no wake sources - only manual reset or power cycle will wake
  // This is the simplest approach - no timers, no GPIO wake
  esp_deep_sleep_start();
  
  // Device will restart from setup() when it wakes
}

// Forward declarations
void handleMainMenu();
void handleSettingsMenu();
void handleRingtoneSelect();
void handleWiFiSetupMenu();
void handleWiFiSSIDInput();
void handleWiFiPasswordInput();
void handleWiFiConnecting();
void handleWiFiStatus();
void handleOTAChecking();
void handleOTAUpdating();
void handleVillageMenu();
void handleVillageCreate();
void handleVillageJoinName();
void handleVillageJoinPassword();
void handlePasswordInput();
void handleUsernameInput();
void handleViewMembers();
void handleMessaging();
void handleMessageCompose();
void dumpMessageStoreDebug(int completedPhase);

// Message callback
void onMessageReceived(const Message& msg) {
  Serial.println("[Message] From " + msg.sender + ": " + msg.content + " (village: " + msg.villageId + ")");
  
  // Check if this message is for the currently loaded village
  bool isForCurrentVillage = village.isInitialized() && (String(village.getVillageId()) == msg.villageId);
  
  // Check if this message already exists in storage to determine if it's truly new
  bool isNewMessage = !village.messageIdExists(msg.messageId);
  
  // ===== SYNC DEBUG: Log every incoming message during sync =====
  int syncPhase = mqttMessenger.getCurrentSyncPhase();
  if (syncPhase > 0) {
    Serial.println("[SYNC DEBUG] Receiving msg: ID=" + msg.messageId + 
                   " from=" + msg.sender + 
                   " isNew=" + String(isNewMessage ? "YES" : "NO") + 
                   " phase=" + String(syncPhase));
  }
  
  // PRESERVE ORIGINAL TIMESTAMP - no adjustment needed now that we use NTP time
  // All devices share the same Unix timestamp baseline
  
  // SMART CACHE LAYER: Decide whether to update UI based on sync state and message novelty
  bool shouldUpdateUI = false;
  
  if (syncPhase == 0) {
    // Not syncing - this is a real-time message, always show it
    shouldUpdateUI = true;
    Serial.println("[Message] Real-time message - updating UI");
  } else if (syncPhase == 1 && isNewMessage) {
    // Phase 1 (recent 20) AND truly new - show it so user sees recent missed messages
    shouldUpdateUI = true;
    Serial.println("[Message] Phase 1 sync - new message found, updating UI");
  } else {
    // Background sync (phase 2+) OR duplicate in phase 1 - silent persistence only
    shouldUpdateUI = false;
    Serial.println("[Message] Background sync or duplicate - silent save only (no UI update)");
  }
  
  // Save message - only save to active village if it matches, otherwise skip UI update
  if (isForCurrentVillage) {
    // Message is for current village - save and optionally update UI
    village.saveMessage(msg);
    
    // Conditionally update UI
    if (shouldUpdateUI) {
      ui.addMessage(msg);
      Serial.println("[Message] Added to UI. Total messages in history: " + String(ui.getMessageCount()));
      
      // Play ringtone if: real-time message AND not viewing this conversation AND ringtone enabled
      bool isRealTime = (syncPhase == 0);
      bool notViewingConversation = !(appState == APP_MESSAGING && inMessagingScreen);
      if (isRealTime && notViewingConversation && isNewMessage) {
        playRingtone();
      }
    } else {
      Serial.println("[Message] Silently cached (not added to UI)");
    }
    
    // For incoming messages (not our own), ensure status is persisted as MSG_RECEIVED (status 2)
    // This happens for all received messages, regardless of which screen we're on
    if (!isSyncing && msg.received && msg.status == MSG_RECEIVED) {
      village.updateMessageStatus(msg.messageId, MSG_RECEIVED);
      Serial.println("[Message] Marked incoming message as received (status 2)");
    }
  } else {
    // Message is for a different village - save to messages.dat without updating UI
    Serial.println("[Message] Message for different village (" + msg.villageId + ") - saving to storage only");
    Village::saveMessageToFile(msg);  // Use static method to save without loading village
    
    // Play ringtone for messages in other conversations (if real-time and new)
    bool isRealTime = (syncPhase == 0);
    if (isRealTime && isNewMessage) {
      playRingtone();
    }
  }
  
  // Only mark as read if this is a NEW message (not a synced historical message)
  // AND if we're actively viewing the messaging screen AND it's for the current village
  // This upgrades the message from MSG_RECEIVED (status 2) to MSG_READ (status 3)
  if (!isSyncing && msg.received && appState == APP_MESSAGING && inMessagingScreen && isForCurrentVillage) {
    Serial.println("[App] Already in messaging screen, marking NEW message as read (status 3)");
    
    // Mark message as read locally
    ui.updateMessageStatus(msg.messageId, MSG_READ);
    village.updateMessageStatus(msg.messageId, MSG_READ);
    
    // Mark that we just transmitted (the ACK)
    lastTransmission = millis();
    
    // Queue read receipt for background sending
    if (!msg.senderMAC.isEmpty()) {
      ReadReceiptQueueItem item;
      item.messageId = msg.messageId;
      item.recipientMAC = msg.senderMAC;
      readReceiptQueue.push_back(item);
      Serial.println("[App] Queued immediate read receipt for: " + msg.messageId);
    }
    
    smartDelay(100);  // Brief delay after transmission
  }
  
  // Always update display when in messaging screen (even if not marked as read yet)
  if (appState == APP_MESSAGING && inMessagingScreen) {
    ui.update();  // Use full update to ensure messages appear
  }
}

void onMessageAcked(const String& messageId, const String& fromMAC) {
  Serial.println("[Message] ACK received for: " + messageId + " from " + fromMAC);
  
  // Update storage FIRST - this is the source of truth and always works
  if (!isSyncing) {
    village.updateMessageStatus(messageId, MSG_RECEIVED);  // Persist to storage (skip during sync)
  }
  
  // Then try to update UI - this may fail if message not in UI yet (race condition with message save)
  ui.updateMessageStatus(messageId, MSG_RECEIVED);
  
  // Update display if we're actively viewing messaging screen OR if we're in village/main menu
  // (so status changes are visible even when not actively in the messaging screen)
  if (inMessagingScreen || appState == APP_VILLAGE_MENU || appState == APP_MAIN_MENU) {
    ui.updatePartial();
  }
}

void onMessageReadReceipt(const String& messageId, const String& fromMAC) {
  Serial.println("[Message] Read receipt for: " + messageId + " from " + fromMAC);
  
  // Update storage FIRST - this is the source of truth and always works
  if (!isSyncing) {
    village.updateMessageStatus(messageId, MSG_READ);  // Persist to storage (skip during sync)
  }
  
  // Then try to update UI - this may fail if message not in UI yet (race condition with message save)
  ui.updateMessageStatus(messageId, MSG_READ);
  
  // Update display if we're actively viewing messaging screen OR if we're in village/main menu
  // (so status changes are visible even when not actively in the messaging screen)
  if (inMessagingScreen || appState == APP_VILLAGE_MENU || appState == APP_MAIN_MENU) {
    ui.updatePartial();
  }
}

void onCommandReceived(const String& command) {
  Serial.println("[Command] Received: " + command);
  logger.info("Command: " + command);
  
  if (command == "update") {
    Serial.println("[Command] Critical update requested");
    logger.info("Critical update command received");
    
    // Only show update screen if on main menu (not interrupting active use)
    if (appState == APP_MAIN_MENU || appState == APP_VILLAGE_MENU) {
      if (otaUpdater.checkForUpdate()) {
        logger.info("OTA: Critical update available: " + otaUpdater.getLatestVersion());
        appState = APP_OTA_CHECKING;
        ui.setState(STATE_OTA_CHECK);
        String updateInfo = "CRITICAL UPDATE\n\n";
        updateInfo += "New: " + otaUpdater.getLatestVersion() + "\n";
        updateInfo += "Current: " + otaUpdater.getCurrentVersion() + "\n\n";
        updateInfo += "Press RIGHT to continue";
        ui.setInputText(updateInfo);
        ui.update();
      } else {
        logger.info("OTA: No update available");
      }
    } else {
      logger.info("OTA: Update command ignored - user is busy");
    }
  } else if (command == "reboot") {
    Serial.println("[Command] Rebooting device...");
    logger.info("Rebooting via MQTT command");
    smartDelay(1000);
    ESP.restart();
  } else if (command == "dump") {
    Serial.println("[Command] Dumping message store state...");
    dumpMessageStoreDebug(0);  // 0 = manual dump, not sync-triggered
  } else {
    Serial.println("[Command] Unknown command: " + command);
    logger.error("Unknown command: " + command);
  }
}

// Debug function to dump message store state after sync phase completes
void dumpMessageStoreDebug(int completedPhase) {
  char myMAC[13];
  sprintf(myMAC, "%012llx", ESP.getEfuseMac());
  
  std::vector<Message> allMessages = village.loadMessages();
  
  Serial.println("========================================");
  Serial.println("[SYNC DEBUG] Device " + String(myMAC) + " AFTER Phase " + String(completedPhase) + " complete");
  Serial.println("[SYNC DEBUG] Total messages in storage NOW: " + String(allMessages.size()));
  Serial.println("[SYNC DEBUG] Message IDs NOW in store (chronological order):");
  
  for (int i = 0; i < allMessages.size() && i < 50; i++) {  // Limit to 50 to avoid spam
    Serial.println("  [" + String(i) + "] ID=" + allMessages[i].messageId + 
                   " from=" + allMessages[i].sender + 
                   " time=" + String(allMessages[i].timestamp) + 
                   " status=" + String((int)allMessages[i].status));
  }
  
  if (allMessages.size() > 50) {
    Serial.println("  ... (" + String(allMessages.size() - 50) + " more messages)");
  }
  
  Serial.println("[SYNC DEBUG] UI message count: " + String(ui.getMessageCount()));
  Serial.println("========================================");
}

// Handle sync request from other device
void onSyncRequest(const String& requestorMAC, unsigned long requestedTimestamp) {
  Serial.println("[Sync] Request from " + requestorMAC + " for messages after timestamp: " + String(requestedTimestamp));
  logger.info("Sync from " + requestorMAC + " (after t=" + String(requestedTimestamp) + ")");
  
  // Ignore sync requests from ourselves
  char myMACStr[13];
  sprintf(myMACStr, "%012llx", ESP.getEfuseMac());
  if (requestorMAC.equalsIgnoreCase(String(myMACStr))) {
    Serial.println("[Sync] Ignoring sync request from self");
    return;
  }
  
  // Load messages newer than requested timestamp
  std::vector<Message> allMessages = village.loadMessages();
  std::vector<Message> newMessages;
  
  Serial.println("[Sync] DEBUG: Filtering " + String(allMessages.size()) + " messages");
  for (const Message& msg : allMessages) {
    Serial.println("[Sync] DEBUG: msg.id=" + msg.messageId + " ts=" + String(msg.timestamp) + " vs requested=" + String(requestedTimestamp) + " isEmpty=" + String(msg.messageId.isEmpty()));
    // Filter: Must have message ID AND be newer than requested timestamp
    if (!msg.messageId.isEmpty() && msg.timestamp > requestedTimestamp) {
      Serial.println("[Sync] DEBUG: INCLUDED");
      newMessages.push_back(msg);
    } else {
      Serial.println("[Sync] DEBUG: SKIPPED");
    }
  }
  
  if (newMessages.empty()) {
    Serial.println("[Sync] No new messages to send (all messages <= " + String(requestedTimestamp) + ")");
    logger.info("Sync: No new messages for " + requestorMAC);
    return;
  }
  
  // Debug output
  char myMAC[13];
  sprintf(myMAC, "%012llx", ESP.getEfuseMac());
  Serial.println("========================================");
  Serial.println("[SYNC] Device " + String(myMAC) + " sending to " + requestorMAC);
  Serial.println("[SYNC] Total messages in storage: " + String(allMessages.size()));
  Serial.println("[SYNC] Messages after t=" + String(requestedTimestamp) + ": " + String(newMessages.size()));
  Serial.println("[SYNC] Sending message IDs:");
  for (int i = 0; i < newMessages.size(); i++) {
    Serial.println("  [" + String(i) + "] ID=" + newMessages[i].messageId + 
                   " from=" + newMessages[i].sender + 
                   " time=" + String(newMessages[i].timestamp) + 
                   " status=" + String((int)newMessages[i].status));
  }
  Serial.println("========================================");
  
  logger.info("Sync: Sending " + String(newMessages.size()) + " msgs to " + requestorMAC);
  
  // Send response with phase=1 (simplified, no multi-phase sync)
  mqttMessenger.sendSyncResponse(requestorMAC, newMessages, 1);
}

void onVillageNameReceived(const String& villageId, const String& villageName) {
  Serial.println("[Village] Received village name announcement for " + villageId + ": " + villageName);
  
  // Find which slot has this village ID
  int slot = Village::findVillageSlotById(villageId);
  if (slot < 0) {
    Serial.println("[Village] WARNING: No slot found for village ID " + villageId);
    return;
  }
  
  // Load the village, update name, and save back
  Village tempVillage;
  if (!tempVillage.loadFromSlot(slot)) {
    Serial.println("[Village] WARNING: Failed to load village from slot " + String(slot));
    return;
  }
  
  tempVillage.setVillageName(villageName);
  if (tempVillage.saveToSlot(slot)) {
    Serial.println("[Village] Updated name in slot " + String(slot) + " to: " + villageName);
    
    // If this is the current village, update UI and MQTT
    if (slot == currentVillageSlot) {
      village.setVillageName(villageName);
      ui.setExistingVillageName(villageName);
      ui.update();  // Force full update to show new name
      mqttMessenger.setVillageInfo(villageId, villageName, village.getUsername());
    }
  } else {
    Serial.println("[Village] WARNING: Failed to save updated name to slot " + String(slot));
  }
}

void setup() {
  // Enable Vext power for peripherals
  pinMode(18, OUTPUT);
  digitalWrite(18, HIGH);
  smartDelay(100);
  
  Serial.begin(115200);
  smartDelay(1000);
  
  // Initialize buzzer
  ledcSetup(BUZZER_CHANNEL, 2000, 8);  // 2000 Hz, 8-bit resolution
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  Serial.print("[Buzzer] Initialized on GPIO ");
  Serial.print(BUZZER_PIN);
  Serial.print(" using LEDC channel ");
  Serial.println(BUZZER_CHANNEL);
  
  Serial.println("\n\n\n\n\n");  // Extra newlines to ensure visibility
  Serial.println("=================================");
  Serial.println("SmolTxt - Safe Texting for Kids");
  Serial.println("=================================");
  Serial.println("Boot starting...");
  smartDelay(500);  // Give time to see messages
  
  // Initialize logger FIRST to capture all subsequent boot events
  Serial.println("[Logger] Initializing event logger...");
  if (!logger.begin()) {
    Serial.println("[Logger] WARNING - Failed to initialize!");
  } else {
    Serial.println("[Logger] Success! Event logging active");
  }
  logger.info("System boot started");
  logger.info("Build: " + String(BUILD_NUMBER));
  Serial.flush();
  smartDelay(100);
  
  Serial.flush();
  smartDelay(100);
  
  // Initialize display
  Serial.println("[Display] Initializing e-paper...");
  Serial.flush();
  if (!ui.begin(EPD_SCK, EPD_MISO, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {
    Serial.println("[Display] ERROR - Failed to initialize!");
    while(1) { smartDelay(1000); }
  }
  Serial.println("[Display] Success!");
  
  // Show splash screen
  Serial.println("[Display] Showing splash...");
  ui.setState(STATE_SPLASH);
  ui.updateClean();  // Clean transition at launch
  smartDelay(2000);
  
  // Initialize I2C for keyboard
  Serial.println("[I2C] Initializing I2C bus...");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(200);  // Use longer timeout for device detection
  Serial.println("[I2C] I2C initialized at 100kHz with 200ms timeout for detection");
  
  // Initialize keyboard
  Serial.println("[Keyboard] Initializing CardKB...");
  keyboard.begin();
  
  // Now set shorter timeout for normal operations to prevent infinite blocking
  Wire.setTimeOut(50);
  Serial.println("[I2C] Timeout reduced to 50ms for normal operations");
  
  // IMPORTANT: Clear keyboard buffer and let I2C settle to prevent phantom key presses
  smartDelay(100);  // Let I2C bus stabilize
  Serial.println("[Keyboard] Before clear - checking for garbage...");
  keyboard.update();  // Read and discard any garbage
  if (keyboard.hasInput()) {
    Serial.println("[Keyboard] WARNING: Found garbage data in buffer!");
    Serial.println("[Keyboard] Garbage: '" + keyboard.getInput() + "'");
  }
  keyboard.clearInput();
  Serial.print("[Keyboard] After clear - currentKey check: ");
  Serial.println(keyboard.isRightPressed() ? "RIGHT PRESSED!" : "no keys");
  Serial.println("[Keyboard] Buffer cleared and ready");
  
  // Initialize messaging screen flag
  inMessagingScreen = false;
  lastMessagingActivity = 0;
  currentVillageSlot = -1;  // No village loaded yet
  
  // Set typing check callback to defer display updates during typing
  ui.setTypingCheckCallback(isUserTyping);
  
  // Set build number for display
  ui.setBuildNumber(BUILD_NUMBER);
  
  // Initialize battery monitoring
  Serial.println("[Battery] Initializing battery monitor...");
  battery.begin();
  battery.update();  // Take initial reading
  ui.setBatteryStatus(battery.getVoltage(), battery.getPercent());
  Serial.println("[Battery] Battery monitor ready");
  
  // Initialize WiFi manager
  Serial.println("[WiFi] Initializing WiFi manager...");
  wifiManager.begin();
  Serial.println("[WiFi] WiFi manager ready");
  
  // Initialize OTA updater
  Serial.println("[OTA] Initializing OTA updater...");
  otaUpdater.begin(&logger);
  otaUpdater.setGitHubRepo("zacknorman-dev", "SmallText");
  Serial.println("[OTA] OTA updater ready");
  
  // Check if WiFi is configured - if so, connect
  if (wifiManager.hasCredentials()) {
    Serial.println("[WiFi] Found saved credentials, connecting...");
    if (wifiManager.connect()) {
      Serial.println("[WiFi] Connected: " + wifiManager.getIPAddress());
      logger.info("WiFi connected: " + wifiManager.getIPAddress());
      
      // Initialize MQTT messenger after WiFi connects
      Serial.println("[MQTT] Initializing MQTT messenger...");
      if (mqttMessenger.begin()) {
        Serial.println("[MQTT] MQTT messenger ready");
        logger.info("MQTT messenger initialized");
        
        // Set up MQTT callbacks
        mqttMessenger.setMessageCallback(onMessageReceived);
        mqttMessenger.setAckCallback(onMessageAcked);
        mqttMessenger.setReadCallback(onMessageReadReceipt);
        mqttMessenger.setCommandCallback(onCommandReceived);
        mqttMessenger.setSyncRequestCallback(onSyncRequest);
        mqttMessenger.setVillageNameCallback(onVillageNameReceived);
        
        // Set encryption
        mqttMessenger.setEncryption(&encryption);
        
        // Subscribe to all saved villages for multi-village support
        mqttMessenger.subscribeToAllVillages();
        Serial.println("[MQTT] Subscribed to all saved villages");
      } else {
        Serial.println("[MQTT] Failed to initialize");
      }
    }
  } else {
    Serial.println("[WiFi] No saved WiFi credentials");
  }
  
  // Note: Timestamp baseline no longer needed - using NTP-synced Unix timestamps
  
  // Rebuild message ID cache for deduplication
  village.rebuildMessageIdCache();
  
  // Request message sync if we have MQTT connection (in case we missed messages while offline)
  if (mqttMessenger.isConnected()) {
    Serial.println("[Sync] Waiting for MQTT subscriptions to propagate...");
    smartDelay(2000);  // Give MQTT subscriptions time to fully activate on broker
    Serial.println("[Sync] Requesting sync from peers");
    mqttMessenger.requestSync(0);  // Request all messages, will deduplicate locally
    smartDelay(1000);  // Give time for sync responses to arrive
  }
  
  // Check for OTA updates on boot (if WiFi connected)
  if (wifiManager.isConnected()) {
    Serial.println("[OTA] Checking for updates on boot...");
    logger.info("OTA: Boot update check");
    if (otaUpdater.checkForUpdate()) {
      logger.info("OTA: New version available: " + otaUpdater.getLatestVersion());
      // Show update screen and let user decide
      appState = APP_OTA_CHECKING;
      ui.setState(STATE_OTA_CHECK);
      String updateInfo = "Update Available\n\n";
      updateInfo += "New: " + otaUpdater.getLatestVersion() + "\n";
      updateInfo += "Current: " + otaUpdater.getCurrentVersion() + "\n\n";
      updateInfo += "Press RIGHT to update\nPress LEFT to skip";
      ui.setInputText(updateInfo);
      ui.update();
      Serial.println("[System] Showing update screen");
      return; // Stay in setup, will continue in loop
    }
  }
  
  // Show village select screen
  Serial.println("[System] Going to village select");
  keyboard.clearInput();  // Clear any stray keys that might trigger typing detection
  Serial.println("[System] Keyboard cleared before village select");
  
  // Auto-load the most recently used village so MQTT is subscribed immediately
  // This allows receiving messages even when sitting at main menu
  if (currentVillageSlot >= 0 && Village::hasVillageInSlot(currentVillageSlot)) {
    Serial.println("[System] Auto-loading last village from slot " + String(currentVillageSlot));
    if (village.loadFromSlot(currentVillageSlot)) {
      encryption.setKey(village.getEncryptionKey());
      Serial.println("[System] Village auto-loaded: " + village.getVillageName());
      logger.info("Auto-loaded village: " + village.getVillageName());
      // MQTT subscription will be configured automatically in main loop
    }
  } else {
    Serial.println("[System] No previous village to auto-load");
  }
  
  appState = APP_MAIN_MENU;
  ui.setState(STATE_VILLAGE_SELECT);
  ui.resetMenuSelection();
  Serial.println("[System] About to call ui.update() for village select...");
  ui.updateClean();  // Clean transition to main menu
  Serial.println("[System] Village select displayed");
  
  Serial.println("[System] Setup complete!");
}

void loop() {
  // Update logger (checks for serial connection, processes commands)
  logger.update();
  
  // Update WiFi manager (handles auto-reconnection)
  wifiManager.update();
  
  // Track if user is actively typing (only if keyboard is present)
  bool hadInput = keyboard.isKeyboardPresent() ? keyboard.hasInput() : false;
  
  keyboard.update();
  
  // Update typing timestamp if new input detected
  if (keyboard.isKeyboardPresent() && keyboard.hasInput() && !hadInput) {
    lastKeystroke = millis();
  }
  
  // Check for messaging screen timeout (clear flag if inactive for too long)
  if (inMessagingScreen && (millis() - lastMessagingActivity > MESSAGING_TIMEOUT)) {
    Serial.println("[App] Messaging screen timeout - clearing flag");
    inMessagingScreen = false;
  }
  
  // Set active village for sending messages (all villages remain subscribed for receiving)
  static String lastActiveVillageId = "";
  if (village.isInitialized()) {
    String currentVillageId = village.getVillageId();
    if (currentVillageId != lastActiveVillageId) {
      // Village changed - set as active for sending
      mqttMessenger.setActiveVillage(currentVillageId);
      lastActiveVillageId = currentVillageId;
      Serial.println("[Loop] Active village set to: " + village.getVillageName());
    }
  } else if (!lastActiveVillageId.isEmpty()) {
    // Village was cleared - reset tracking
    lastActiveVillageId = "";
  }
  
  // Process incoming MQTT messages
  mqttMessenger.loop();
  
  // Update battery readings (rate-limited internally)
  battery.update();
  ui.setBatteryStatus(battery.getVoltage(), battery.getPercent());
  
  // Check for shutdown using Tab key held for 3 seconds
  // Tab key is 0x09 - simple and rarely used in normal operation
  bool tabCurrentlyHeld = keyboard.isTabHeld();
  
  if (!isShuttingDown && tabCurrentlyHeld) {
    if (shutdownHoldStart == 0) {
      shutdownHoldStart = millis();
      lastShutdownKey = 'T';  // Mark as Tab key
      Serial.println("[Power] Tab key hold detected - hold for 3s to sleep");
    }
    
    unsigned long holdDuration = millis() - shutdownHoldStart;
    
    // Check if we've reached 3 seconds - trigger shutdown immediately while still holding
    if (holdDuration >= SHUTDOWN_HOLD_TIME && !isShuttingDown) {
      isShuttingDown = true;
      Serial.println("[Power] Shutdown triggered! (3s hold complete)");
      enterDeepSleep();
      // Never returns - device enters deep sleep
    }
    // Show progress every second while holding
    else if (holdDuration >= 2000 && holdDuration < 3000 && lastShutdownKey != '2') {
      Serial.println("[Power] 1 more second... (holdDuration=" + String(holdDuration) + "ms)");
      lastShutdownKey = '2';  // Mark we've shown 2s message
    } else if (holdDuration >= 1000 && holdDuration < 2000 && lastShutdownKey != '1') {
      Serial.println("[Power] 2 more seconds... (holdDuration=" + String(holdDuration) + "ms)");
      lastShutdownKey = '1';  // Mark we've shown 1s message
    }
  } else {
    // Tab key released - reset timer only if not already shutting down
    if (shutdownHoldStart != 0 && !isShuttingDown) {
      Serial.println("[Power] Shutdown cancelled - Tab key released (tabHeld=" + String(tabCurrentlyHeld) + ")");
      shutdownHoldStart = 0;
      lastShutdownKey = 0;
    }
  }
  
  // Process read receipt queue in background (send one per loop iteration)
  static unsigned long lastReadReceiptSent = 0;
  if (!readReceiptQueue.empty() && (millis() - lastTransmission > 150) && (millis() - lastReadReceiptSent > 150)) {
    ReadReceiptQueueItem item = readReceiptQueue.front();
    readReceiptQueue.erase(readReceiptQueue.begin());
    
    Serial.println("[App] Sending queued read receipt for: " + item.messageId);
    mqttMessenger.sendReadReceipt(item.messageId, item.recipientMAC);
    
    lastReadReceiptSent = millis();
    lastTransmission = millis();
  }
  
  switch (appState) {
    case APP_MAIN_MENU:
      static bool loggedMainMenu = false;
      if (!loggedMainMenu) {
        Serial.println("[Loop] Entering handleMainMenu for first time");
        loggedMainMenu = true;
      }
      handleMainMenu();
      break;
    case APP_SETTINGS_MENU:
      handleSettingsMenu();
      break;
    case APP_RINGTONE_SELECT:
      handleRingtoneSelect();
      break;
    case APP_WIFI_SETUP_MENU:
      handleWiFiSetupMenu();
      break;
    case APP_WIFI_SSID_INPUT:
      handleWiFiSSIDInput();
      break;
    case APP_WIFI_PASSWORD_INPUT:
      handleWiFiPasswordInput();
      break;
    case APP_WIFI_CONNECTING:
      handleWiFiConnecting();
      break;
    case APP_WIFI_STATUS:
      handleWiFiStatus();
      break;
    case APP_OTA_CHECKING:
      handleOTAChecking();
      break;
    case APP_OTA_UPDATING:
      handleOTAUpdating();
      break;
    case APP_VILLAGE_MENU:
      handleVillageMenu();
      break;
    case APP_VILLAGE_CREATE:
      handleVillageCreate();
      break;
    case APP_VILLAGE_JOIN_PASSWORD:
      handleVillageJoinPassword();
      break;
    case APP_VILLAGE_JOIN_NAME:
      handleVillageJoinName();
      break;
    case APP_PASSWORD_INPUT:
      handlePasswordInput();
      break;
    case APP_USERNAME_INPUT:
      handleUsernameInput();
      break;
    case APP_VIEW_MEMBERS:
      handleViewMembers();
      break;
    case APP_MESSAGING:
      handleMessaging();
      break;
    case APP_MESSAGE_COMPOSE:
      handleMessageCompose();
      break;
  }
  
  smartDelay(5);  // Faster loop = more keyboard polling
}

void handleMainMenu() {
  // Check for up/down navigation with arrow keys
  if (keyboard.isUpPressed()) {
    Serial.println("[MainMenu] UP pressed");
    ui.menuUp();
    ui.updatePartial();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    Serial.println("[MainMenu] DOWN pressed");
    ui.menuDown();
    ui.updatePartial();
    smartDelay(200);
  }
  
  // Check for enter or right arrow
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    Serial.println("[MainMenu] ENTER or RIGHT pressed - advancing to selection");
    int selection = ui.getMenuSelection();
    
    // Count existing villages
    int villageCount = 0;
    int villageSlots[10];  // Track which slots have villages
    for (int i = 0; i < 10; i++) {
      if (Village::hasVillageInSlot(i)) {
        villageSlots[villageCount] = i;
        villageCount++;
      }
    }
    
    // Determine what was selected
    if (selection < villageCount) {
      // Selected an existing village - load it
      int slot = villageSlots[selection];
      Serial.println("[MainMenu] Loading village from slot " + String(slot));
      
      if (village.loadFromSlot(slot)) {
        currentVillageSlot = slot;
        ui.setExistingVillageName(village.getVillageName());
        encryption.setKey(village.getEncryptionKey());
        
        // Set as active village for sending (already subscribed at boot)
        mqttMessenger.setActiveVillage(village.getVillageId());
        Serial.println("[MainMenu] Active village: " + village.getVillageName());
        
        // Go to village menu
        keyboard.clearInput();
        appState = APP_VILLAGE_MENU;
        ui.setState(STATE_VILLAGE_MENU);
        ui.resetMenuSelection();
        ui.updateClean();  // Clean transition
      }
    } else if (selection == villageCount) {
      // Selected "New Village" - ask for village name first
      isCreatingVillage = true;
      keyboard.clearInput();
      appState = APP_VILLAGE_CREATE;
      ui.setState(STATE_CREATE_VILLAGE);
      ui.setInputText("");
      ui.updateClean();  // Clean transition
    } else if (selection == villageCount + 1) {
      // Selected "Join Village"
      isCreatingVillage = false;
      keyboard.clearInput();
      appState = APP_VILLAGE_JOIN_PASSWORD;
      ui.setState(STATE_JOIN_VILLAGE_PASSWORD);
      ui.setInputText("");
      ui.updateClean();  // Clean transition
    } else if (selection == villageCount + 2) {
      // Selected "Settings"
      keyboard.clearInput();
      appState = APP_SETTINGS_MENU;
      ui.setState(STATE_SETTINGS_MENU);
      ui.resetMenuSelection();
      ui.updateClean();  // Clean transition
    }
    
    smartDelay(300);
  }
}

void handleVillageMenu() {
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updatePartial();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updatePartial();
    smartDelay(200);
  }
  
  // Left arrow to go back to main menu
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.updateClean();  // Clean transition
    smartDelay(300);
    return;
  }
  
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    
    if (selection == 0) {
      // Messages
      Serial.println("[App] Entering messaging. Messages in history: " + String(ui.getMessageCount()));
      keyboard.clearInput();  // Clear buffer to prevent typing detection freeze
      appState = APP_MESSAGING;
      inMessagingScreen = true;  // Set flag - we're now viewing messages
      lastMessagingActivity = millis();  // Record activity time
      ui.setCurrentUsername(village.getUsername());  // Set username for message display
      ui.setState(STATE_MESSAGING);
      ui.resetMessageScroll();  // Reset scroll to show latest messages
      
      // Request message sync when entering messaging screen
      std::vector<Message> existingMsgs = village.loadMessages();
      unsigned long lastMsgTime = 0;
      for (const auto& msg : existingMsgs) {
        if (msg.timestamp > lastMsgTime) {
          lastMsgTime = msg.timestamp;
        }
      }
      if (mqttMessenger.isConnected()) {
        Serial.println("[Sync] Requesting sync on entering messages: last timestamp=" + String(lastMsgTime));
        logger.info("Sync: Request sent, last=" + String(lastMsgTime));
        mqttMessenger.requestSync(lastMsgTime);
        smartDelay(500);  // Wait for sync responses to arrive
      }
      
      // Load messages with pagination - show last N messages (same window for both devices)
      ui.clearMessages();  // Clear any old messages from UI
      std::vector<Message> messages = village.loadMessages();
      Serial.println("[Village] Loaded " + String(messages.size()) + " messages from storage");
      
      // Calculate pagination: always show last MAX_MESSAGES_TO_LOAD messages
      // This ensures both devices see the SAME window of recent conversation
      int startIndex = messages.size() > MAX_MESSAGES_TO_LOAD ? messages.size() - MAX_MESSAGES_TO_LOAD : 0;
      int displayCount = 0;
      
      // Add paginated messages to UI (same chunk for all devices)
      for (int i = startIndex; i < messages.size(); i++) {
        ui.addMessage(messages[i]);
        displayCount++;
      }
      Serial.println("[App] Displaying last " + String(displayCount) + " of " + String(messages.size()) + " messages (paginated, consistent across devices)");
      
      // Mark all received (but not yet read) messages as read and queue read receipts
      readReceiptQueue.clear();  // Clear any old queue items
      int unreadCount = 0;
      for (int i = startIndex; i < messages.size(); i++) {
        const Message& msg = messages[i];
        // Only process received messages that aren't already read
        if (msg.received && msg.status == MSG_RECEIVED && !msg.messageId.isEmpty()) {
          // Mark as read in UI and storage
          ui.updateMessageStatus(msg.messageId, MSG_READ);
          village.updateMessageStatus(msg.messageId, MSG_READ);
          
          // Queue read receipt to sender
          if (!msg.senderMAC.isEmpty()) {
            ReadReceiptQueueItem item;
            item.messageId = msg.messageId;
            item.recipientMAC = msg.senderMAC;
            readReceiptQueue.push_back(item);
            unreadCount++;
          }
        }
      }
      if (unreadCount > 0) {
        Serial.println("[App] Marked " + String(unreadCount) + " unread messages as read, queued receipts");
      }
      
      ui.update();  // Always refresh to show any messages received
    } else if (selection == 1) {
      // Add Member (owner only)
      // TODO: Implement add member screen
    } else if (selection == 2) {
      // View Members
      appState = APP_VIEW_MEMBERS;
      ui.setState(STATE_VIEW_MEMBERS);
      ui.setMemberList(village.getMemberList());
      ui.updateClean();  // Clean transition
    } else if (selection == 3) {
      // Leave Village
      ui.showMessage("Leave Village?", "Press ENTER to confirm\nor LEFT to cancel", 0);
      
      while (true) {
        keyboard.update();
        if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
          // Confirmed - delete village slot
          if (currentVillageSlot >= 0) {
            String villageId = village.getVillageId();
            Village::deleteSlot(currentVillageSlot);
            
            // Remove from MQTT subscriptions
            if (!villageId.isEmpty()) {
              mqttMessenger.removeVillageSubscription(villageId);
              Serial.println("[VillageMenu] Removed village from MQTT subscriptions");
            }
          }
          village.clearVillage();
          currentVillageSlot = -1;
          
          ui.showMessage("Village", "Left village", 1500);
          smartDelay(1500);
          
          appState = APP_MAIN_MENU;
          ui.setState(STATE_VILLAGE_SELECT);
          ui.resetMenuSelection();
          ui.updateClean();  // Clean transition
          break;
        } else if (keyboard.isLeftPressed()) {
          ui.setState(STATE_VILLAGE_MENU);
          ui.updateClean();  // Clean transition
          break;
        }
        smartDelay(50);
      }
    }
    
    smartDelay(300);
  }
}

void handleViewMembers() {
  keyboard.update();
  
  // Left arrow to go back to village menu
  if (keyboard.isLeftPressed()) {
    appState = APP_VILLAGE_MENU;
    ui.setState(STATE_VILLAGE_MENU);
    ui.resetMenuSelection();
    ui.updateClean();  // Clean transition
    smartDelay(300);
  }
}

void handleVillageCreate() {
  keyboard.update();
  
  // Left arrow to cancel and go back
  if (keyboard.isLeftPressed()) {
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Handle backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Handle enter - save custom village name
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String currentName = ui.getInputText();
    if (currentName.length() > 0) {
      // Store user's custom village name (this will be used as actual name)
      tempVillageName = currentName;
      
      // Now ask for username (passphrase will be generated after username)
      appState = APP_USERNAME_INPUT;
      ui.setState(STATE_INPUT_USERNAME);
      ui.setInputText("");
      ui.update();
    }
    smartDelay(300);
    return;
  }
  
  // Check for regular input - accumulate ALL pending characters
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 20) {
        ui.addInputChar(c);
      }
    }
    
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handleVillageJoinPassword() {
  keyboard.update();
  
  // Left arrow to go back to main menu
  if (keyboard.isLeftPressed()) {
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Handle backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Handle enter - join with passphrase only
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String passphrase = ui.getInputText();
    if (passphrase.length() > 0) {
      // Normalize to lowercase for case-insensitive matching
      passphrase.toLowerCase();
      tempVillagePassword = passphrase;
      
      // Don't derive a name - let it be set via MQTT announcement from creator
      tempVillageName = "";  // Will be updated via MQTT retained message
      Serial.println("[Join] Passphrase entered: " + passphrase);
      Serial.println("[Join] Village name will be received via MQTT");
      
      // Go straight to username - village name will update in background
      ui.setExistingVillageName("Joining...");
      appState = APP_USERNAME_INPUT;
      ui.setState(STATE_INPUT_USERNAME);
      ui.setInputText("");
      ui.update();
    }
    smartDelay(300);
    return;
  }
  
  // Check for regular input
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 30) {
        ui.addInputChar(c);
      }
    }
    
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handleVillageJoinName() {
  keyboard.update();
  
  // Left arrow to cancel and go back to passphrase
  if (keyboard.isLeftPressed()) {
    appState = APP_VILLAGE_JOIN_PASSWORD;
    ui.setState(STATE_JOIN_VILLAGE_PASSWORD);
    ui.setInputText(tempVillagePassword);  // Restore passphrase
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Handle backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Handle enter - save village name and join
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String villageName = ui.getInputText();
    if (villageName.length() > 0) {
      tempVillageName = villageName;
      
      // DON'T create village here - just store the name
      // Village will be created/joined properly after username input
      // This prevents duplicate villages from being created in different slots
      ui.setExistingVillageName(tempVillageName);
      
      // Ask for username
      appState = APP_USERNAME_INPUT;
      ui.setState(STATE_INPUT_USERNAME);
      ui.setInputText("");
      ui.update();
    }
    smartDelay(300);
    return;
  }
  
  // Check for regular input
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 30) {
        ui.addInputChar(c);
      }
    }
    
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handlePasswordInput() {
  keyboard.update();
  
  // Left arrow to cancel and go back
  if (keyboard.isLeftPressed()) {
    appState = APP_VILLAGE_CREATE;
    ui.setState(STATE_CREATE_VILLAGE);
    ui.setInputText(tempVillageName);  // Restore village name
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Handle backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Handle enter - save password and go to username
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String currentPassword = ui.getInputText();
    if (currentPassword.length() > 0) {
      tempVillagePassword = currentPassword;
      
      // Now ask for username
      appState = APP_USERNAME_INPUT;
      ui.setState(STATE_INPUT_USERNAME);
      ui.setInputText("");
      ui.update();
    }
    smartDelay(300);
    return;
  }
  
  // Check for regular input
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 30) {
        ui.addInputChar(c);
      }
    }
    
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handleUsernameInput() {
  keyboard.update();
  
  // Left arrow to cancel and go back
  if (keyboard.isLeftPressed()) {
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Handle backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Handle enter
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String currentName = ui.getInputText();
    if (currentName.length() > 0) {
      if (isCreatingVillage) {
        // Generate passphrase for new village
        tempVillagePassword = village.generatePassphrase();
        
        Serial.println("[Create] Generated passphrase: " + tempVillagePassword);
        Serial.println("[Create] Custom village name: " + tempVillageName);
        
        // Clear memory and create the new village with custom name
        village.clearVillage();
        village.createVillage(tempVillageName, tempVillagePassword);
        ui.setExistingVillageName(tempVillageName);
      } else {
        // For joining, create the village with the passphrase provided
        village.clearVillage();
        village.joinVillageAsMember(tempVillageName, tempVillagePassword);
      }
      
      village.setUsername(currentName);
      
      // Find appropriate slot for this village
      currentVillageSlot = -1;  // Reset slot search
      
      // For joiners, check if village already exists in a slot (by UUID derived from password)
      if (!isCreatingVillage) {
        String villageId = village.getVillageId();  // Get the ID that was set during joinVillageAsMember
        currentVillageSlot = Village::findVillageSlotById(villageId);
        Serial.println("[Main] Joiner searching for village ID: " + villageId + ", found in slot: " + String(currentVillageSlot));
      } else {
        // For creators, also check if we somehow already created this village
        String villageId = village.getVillageId();
        currentVillageSlot = Village::findVillageSlotById(villageId);
        Serial.println("[Main] Creator checking for existing village ID: " + villageId + ", found in slot: " + String(currentVillageSlot));
      }
      
      // If not found, find first empty slot
      if (currentVillageSlot == -1) {
        for (int i = 0; i < 10; i++) {
          if (!Village::hasVillageInSlot(i)) {
            currentVillageSlot = i;
            Serial.println("[Main] Using empty slot: " + String(i));
            break;
          }
        }
      } else {
        Serial.println("[Main] Reusing existing slot: " + String(currentVillageSlot));
      }
      
      if (currentVillageSlot == -1) {
        // All slots full - use slot 0 (overwrite oldest)
        currentVillageSlot = 0;
        Serial.println("[Village] All slots full, overwriting slot 0");
      }
      
      Serial.println("[Village] Saving to slot " + String(currentVillageSlot));
      village.saveToSlot(currentVillageSlot);
      
      encryption.setKey(village.getEncryptionKey());
      
      // Add new village to MQTT subscriptions and set as active
      mqttMessenger.addVillageSubscription(
        village.getVillageId(),
        village.getVillageName(),
        village.getUsername(),
        village.getEncryptionKey()
      );
      mqttMessenger.setActiveVillage(village.getVillageId());
      Serial.println("[Username] Village added to MQTT subscriptions: " + village.getVillageName());
      
      if (isCreatingVillage) {
        // Creator: Announce village name immediately after creating
        if (mqttMessenger.isConnected()) {
          mqttMessenger.announceVillageName(village.getVillageName());
          Serial.println("[Village] Announced village name: " + village.getVillageName());
        }
        
        // Show passphrase for creators
        String infoMsg = "The secret passphrase for\nthis village is:\n\n";
        infoMsg += tempVillagePassword + "\n\n";
        infoMsg += "Only friends you tell it to\ncan join.\n\n";
        infoMsg += "Press ENTER to continue";
        
        ui.showMessage("Village Created!", infoMsg, 0);
        
        // Wait for enter key to continue
        while (!keyboard.isEnterPressed() && !keyboard.isRightPressed()) {
          keyboard.update();
          smartDelay(50);
        }
        
        Serial.println("[Village] Creator acknowledged passphrase, going to messaging");
      } else {
        // Joiner: Wait briefly for village name announcement
        Serial.println("[Village] Waiting for village name announcement...");
        unsigned long startWait = millis();
        while (millis() - startWait < 1000) {  // Wait up to 1 second
          mqttMessenger.loop();  // Process incoming messages
          smartDelay(50);
        }
        
        // Reload village to get updated name
        village.loadFromSlot(currentVillageSlot);
        
        // For joiners, show welcome screen
        String infoMsg = "Welcome to the village!\n\n";
        infoMsg += "You can now chat with\nother members.\n\n";
        infoMsg += "Press ENTER to continue";
        
        ui.showMessage("Village Joined!", infoMsg, 0);
        
        // Wait for enter key to continue
        while (!keyboard.isEnterPressed() && !keyboard.isRightPressed()) {
          keyboard.update();
          smartDelay(50);
        }
      }
      
      // Properly initialize messaging screen (same as menu path)
      Serial.println("[App] ============================================");
      Serial.println("[App] ENTERING MESSAGING - appState will be set to APP_MESSAGING");
      Serial.println("[App] isCreatingVillage: " + String(isCreatingVillage));
      Serial.println("[App] Village: " + village.getVillageName());
      Serial.println("[App] Username: " + village.getUsername());
      Serial.println("[App] Messages in history: " + String(ui.getMessageCount()));
      Serial.println("[App] ============================================");
      
      keyboard.clearInput();  // Clear buffer to prevent typing detection freeze
      appState = APP_MESSAGING;
      inMessagingScreen = true;  // Set flag - we're now viewing messages
      lastMessagingActivity = millis();  // Record activity time
      ui.setCurrentUsername(village.getUsername());  // Set username for message display
      ui.setState(STATE_MESSAGING);
      ui.resetMessageScroll();  // Reset scroll to show latest messages
      
      // Load messages with pagination - show last N messages (same window for both devices)
      ui.clearMessages();  // Clear any old messages from UI
      std::vector<Message> messages = village.loadMessages();
      Serial.println("[Village] Loaded " + String(messages.size()) + " messages from storage");
      
      // Calculate pagination: always show last MAX_MESSAGES_TO_LOAD messages
      // This ensures both devices see the SAME window of recent conversation
      int startIndex = messages.size() > MAX_MESSAGES_TO_LOAD ? messages.size() - MAX_MESSAGES_TO_LOAD : 0;
      int displayCount = 0;
      
      // Add paginated messages to UI (same chunk for all devices)
      for (int i = startIndex; i < messages.size(); i++) {
        ui.addMessage(messages[i]);
        displayCount++;
      }
      Serial.println("[App] Displaying last " + String(displayCount) + " of " + String(messages.size()) + " messages (paginated, consistent across devices)");
      
      // Mark all received (but not yet read) messages as read and queue read receipts
      readReceiptQueue.clear();  // Clear any old queue items
      int unreadCount = 0;
      for (int i = startIndex; i < messages.size(); i++) {
        const Message& msg = messages[i];
        // Only process received messages that aren't already read
        if (msg.received && msg.status == MSG_RECEIVED && !msg.messageId.isEmpty()) {
          // Mark as read in UI and storage
          ui.updateMessageStatus(msg.messageId, MSG_READ);
          village.updateMessageStatus(msg.messageId, MSG_READ);
          
          // Queue read receipt to sender
          if (!msg.senderMAC.isEmpty()) {
            ReadReceiptQueueItem item;
            item.messageId = msg.messageId;
            item.recipientMAC = msg.senderMAC;
            readReceiptQueue.push_back(item);
            unreadCount++;
          }
        }
      }
      if (unreadCount > 0) {
        Serial.println("[App] Marked " + String(unreadCount) + " unread messages as read, queued receipts");
      }
      
      ui.setInputText("");  // Clear input field before entering messaging
      ui.update();
    }
    smartDelay(300);
    return;
  }
  
  // Accumulate input characters
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 20) {
        ui.addInputChar(c);
      }
    }
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handleMessaging() {
  static unsigned long lastKeyPress = 0;
  static unsigned long KEY_DEBOUNCE = 150;
  
  keyboard.update();
  
  // Debounce check - skip if too soon after last key
  if (millis() - lastKeyPress < KEY_DEBOUNCE) {
    return;
  }
  
  // Left arrow to go back to village menu
  if (keyboard.isLeftPressed()) {
    inMessagingScreen = false;  // Clear flag - leaving messages
    lastMessagingActivity = millis();
    appState = APP_VILLAGE_MENU;
    ui.setState(STATE_VILLAGE_MENU);
    ui.resetMenuSelection();
    ui.setInputText("");  // Clear any typed text
    ui.update();
    lastKeyPress = millis();
    return;
  }
  
  // Arrow keys for scrolling message history
  if (keyboard.isUpPressed()) {
    ui.scrollMessagesUp();
    ui.updatePartial();
    lastMessagingActivity = millis();  // Update timestamp after scrolling
    lastKeyPress = millis();
    return;
  } else if (keyboard.isDownPressed()) {
    ui.scrollMessagesDown();
    ui.updatePartial();
    lastMessagingActivity = millis();  // Update timestamp after scrolling
    lastKeyPress = millis();
    return;
  }
  
  // Handle backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
      lastMessagingActivity = millis();  // Update timestamp after backspace
    }
    lastKeyPress = millis();
    return;
  }
  
  // Enter to send message (if there's text)
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String messageText = ui.getInputText();
    Serial.println("[App] Enter pressed. Input text: '" + messageText + "' length: " + String(messageText.length()));
    if (messageText.length() > 0) {
      // Show "Sending..." feedback immediately
      ui.setInputText("Sending...");
      ui.updatePartial();  // Quick partial update to show sending feedback
      
      // Send the message via MQTT
      
      // Generate a message ID by sending via MQTT
      String sentMessageId = "";
      if (mqttMessenger.isConnected()) {
        sentMessageId = mqttMessenger.sendShout(messageText);
        logger.info("MQTT send: " + (sentMessageId.isEmpty() ? "FAILED" : "SUCCESS id=" + sentMessageId));
      } else {
        logger.error("MQTT not connected - message not sent");
      }
      Serial.println("[App] Sending message via MQTT");
      
      // Add to local message history
      Message localMsg;
      localMsg.sender = village.getUsername();
      localMsg.content = messageText;
      localMsg.timestamp = getCurrentTime();
      localMsg.received = false;
      localMsg.status = MSG_SENT;
      localMsg.messageId = sentMessageId;  // Use the actual ID from MQTT
      localMsg.villageId = String(village.getVillageId());  // Set village ID
      ui.addMessage(localMsg);
      
      // Save to storage
      village.saveMessage(localMsg);
      
      // Clear input and scroll to bottom to show the message we just sent
      ui.setInputText("");
      ui.resetMessageScroll();
      ui.update();  // Full update to show the sent message
      lastMessagingActivity = millis();  // Update timestamp after sending
    }
    lastKeyPress = millis();
    return;
  }
  
  // Type directly into input field
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 130) {
        ui.addInputChar(c);
      }
    }
    
    // Clear the keyboard buffer after processing so we don't re-add same chars
    keyboard.clearInput();
    
    ui.updatePartial();
    lastMessagingActivity = millis();  // Update timestamp after typing
  }
}

void handleMessageCompose() {
  keyboard.update();
  
  // Left arrow to cancel
  if (keyboard.isLeftPressed()) {
    appState = APP_MESSAGING;
    ui.setCurrentUsername(village.getUsername());  // Set username for message display
    ui.setState(STATE_MESSAGING);
    ui.setInputText("");
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Handle backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Handle enter - send message
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String currentText = ui.getInputText();
    if (currentText.length() > 0) {
      logger.info("User sending message: " + currentText.substring(0, 30) + (currentText.length() > 30 ? "..." : ""));
      
      // Show "Sending..." feedback immediately
      ui.setInputText("Sending...");
      ui.updatePartial();  // Quick partial update to show sending feedback
      
      // Send shout via MQTT
      String messageId = "";
      bool mqttConn = mqttMessenger.isConnected();
      logger.info("MQTT send attempt: connected=" + String(mqttConn ? "YES" : "NO"));
      
      if (mqttConn) {
        messageId = mqttMessenger.sendShout(currentText);
        if (!messageId.isEmpty()) {
          Serial.println("[App] Message sent via MQTT: " + messageId);
          logger.info("MQTT send SUCCESS, ID: " + messageId);
        } else {
          Serial.println("[App] MQTT send failed");
          logger.error("MQTT send FAILED - sendShout returned empty ID");
        }
      } else {
        Serial.println("[App] MQTT not connected - message not sent");
        logger.error("MQTT NOT CONNECTED - cannot send");
      }
      
      Message sentMsg;
      sentMsg.sender = village.getUsername();
      sentMsg.content = currentText;
      sentMsg.timestamp = millis();
      sentMsg.received = false;
      sentMsg.messageId = messageId;
      sentMsg.status = messageId.isEmpty() ? MSG_SENT : MSG_SENT;
      sentMsg.villageId = String(village.getVillageId());  // Set village ID
      ui.addMessage(sentMsg);
      
      // Save message to disk
      if (!messageId.isEmpty()) {
        village.saveMessage(sentMsg);
      }
      
      // Clear input and switch to messaging view
      ui.setInputText("");
      appState = APP_MESSAGING;
      ui.setCurrentUsername(village.getUsername());  // Set username for message display
      ui.setState(STATE_MESSAGING);
      ui.update();
    }
    smartDelay(300);
    return;
  }
  
  // Accumulate input characters
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 130) {
        ui.addInputChar(c);
      }
    }
    keyboard.clearInput();
    ui.updatePartial();
  }
}

// ============================================================================
// WiFi & OTA HANDLERS
// ============================================================================

void handleSettingsMenu() {
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updatePartial();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updatePartial();
    smartDelay(200);
  }
  
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // Right arrow to open submenu
  if (keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    
    if (selection == 0) {
      // Open Ringtone selection menu
      keyboard.clearInput();
      appState = APP_RINGTONE_SELECT;
      ui.setState(STATE_RINGTONE_SELECT);
      ui.resetMenuSelection();
      ui.updateClean();
    } else if (selection == 1) {
      // Open WiFi menu
      keyboard.clearInput();
      appState = APP_WIFI_SETUP_MENU;
      ui.setState(STATE_WIFI_SETUP_MENU);
      ui.resetMenuSelection();
      ui.updateClean();
    } else if (selection == 2) {
      // Check for Updates
      keyboard.clearInput();
      appState = APP_OTA_CHECKING;
      ui.setState(STATE_OTA_CHECK);
      ui.setInputText("Checking...\nCurrent: " + String(FIRMWARE_VERSION));
      ui.update();
      
      // Perform check (blocking)
      if (otaUpdater.checkForUpdate()) {
        String updateInfo = "Update Available!\n";
        updateInfo += "New version: " + otaUpdater.getLatestVersion() + "\n";
        updateInfo += "Current: " + otaUpdater.getCurrentVersion();
        ui.setInputText(updateInfo);
      } else {
        String updateInfo = otaUpdater.getStatusString() + "\n";
        updateInfo += "Version: " + otaUpdater.getCurrentVersion();
        ui.setInputText(updateInfo);
      }
      ui.update();
    }
    
    smartDelay(300);
  }
}

void handleRingtoneSelect() {
  static int lastSelection = -1;
  int currentSelection = ui.getMenuSelection();
  
  if (keyboard.isUpPressed()) {
    lastSelection = -1;  // Force preview on next selection
    ui.menuUp();
    ui.updatePartial();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    lastSelection = -1;  // Force preview on next selection
    ui.menuDown();
    ui.updatePartial();
    smartDelay(200);
  }
  
  // Play preview when selection changes
  currentSelection = ui.getMenuSelection();
  if (currentSelection != lastSelection) {
    lastSelection = currentSelection;
    playRingtoneSound((RingtoneType)currentSelection);
  }
  
  // Left arrow - go back without changing selection
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_SETTINGS_MENU;
    ui.setState(STATE_SETTINGS_MENU);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // Right arrow - select and go back
  if (keyboard.isRightPressed()) {
    selectedRingtone = (RingtoneType)currentSelection;
    ringtoneEnabled = (selectedRingtone != RINGTONE_OFF);
    ui.setRingtoneEnabled(ringtoneEnabled);
    ui.setRingtoneName(ringtoneNames[selectedRingtone]);
    Serial.print("[Settings] Ringtone set to: ");
    Serial.println(ringtoneNames[selectedRingtone]);
    
    keyboard.clearInput();
    appState = APP_SETTINGS_MENU;
    ui.setState(STATE_SETTINGS_MENU);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
  }
}

void handleWiFiSetupMenu() {
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updatePartial();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updatePartial();
    smartDelay(200);
  }
  
  // Left arrow to go back to settings
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_SETTINGS_MENU;
    ui.setState(STATE_SETTINGS_MENU);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  if (keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    
    if (selection == 0) {
      // Configure WiFi
      keyboard.clearInput();
      appState = APP_WIFI_SSID_INPUT;
      ui.setState(STATE_WIFI_SSID_INPUT);
      ui.setInputText("");
      ui.update();
    } else if (selection == 1) {
      // Check Connection
      keyboard.clearInput();
      appState = APP_WIFI_STATUS;
      ui.setState(STATE_WIFI_STATUS);
      
      // Format status info
      String status = wifiManager.getStatusString() + "\n";
      if (wifiManager.isConnected()) {
        status += wifiManager.getIPAddress() + "\n";
        status += String(wifiManager.getSignalStrength()) + " dBm";
      } else {
        status += "Not connected";
      }
      ui.setInputText(status);
      ui.update();
    }
    
    smartDelay(300);
  }
}

void handleWiFiSSIDInput() {
  // Left arrow to cancel
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_SETUP_MENU;
    ui.setState(STATE_WIFI_SETUP_MENU);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Enter to continue to password
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    tempWiFiSSID = ui.getInputText();
    if (tempWiFiSSID.length() > 0) {
      keyboard.clearInput();
      appState = APP_WIFI_PASSWORD_INPUT;
      ui.setState(STATE_WIFI_PASSWORD_INPUT);
      ui.setInputText("");
      ui.update();
    }
    smartDelay(300);
    return;
  }
  
  // Accumulate input
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 50) {
        ui.addInputChar(c);
      }
    }
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handleWiFiPasswordInput() {
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_SSID_INPUT;
    ui.setState(STATE_WIFI_SSID_INPUT);
    ui.setInputText(tempWiFiSSID);
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Backspace
  if (keyboard.isBackspacePressed()) {
    if (ui.getInputText().length() > 0) {
      ui.removeInputChar();
      ui.updatePartial();
    }
    smartDelay(150);
    return;
  }
  
  // Enter to save and connect
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    tempWiFiPassword = ui.getInputText();
    if (tempWiFiPassword.length() > 0) {
      // Save credentials
      wifiManager.saveCredentials(tempWiFiSSID, tempWiFiPassword);
      
      // Go to connecting state
      keyboard.clearInput();
      appState = APP_WIFI_CONNECTING;
      ui.setState(STATE_WIFI_STATUS);
      ui.setInputText("Connecting to\n" + tempWiFiSSID + "...");
      ui.update();
      
      // Attempt connection
      if (wifiManager.connectWithCredentials(tempWiFiSSID, tempWiFiPassword)) {
        String status = "Connected!\n";
        status += wifiManager.getIPAddress() + "\n";
        status += String(wifiManager.getSignalStrength()) + " dBm";
        ui.setInputText(status);
        logger.info("WiFi connected: " + wifiManager.getIPAddress());
      } else {
        ui.setInputText("Connection Failed\nCheck credentials");
        logger.error("WiFi connection failed");
      }
      ui.update();
      
      // Wait for user acknowledgment
      appState = APP_WIFI_STATUS;
    }
    smartDelay(300);
    return;
  }
  
  // Accumulate input
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    for (char c : input) {
      if (c >= 32 && c < 127 && ui.getInputText().length() < 50) {
        ui.addInputChar(c);
      }
    }
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handleWiFiConnecting() {
  // Just wait - should transition automatically in handleWiFiPasswordInput
  smartDelay(100);
}

void handleWiFiStatus() {
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_SETUP_MENU;
    ui.setState(STATE_WIFI_SETUP_MENU);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Enter to continue
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_SETUP_MENU;
    ui.setState(STATE_WIFI_SETUP_MENU);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
  }
}

void handleOTAChecking() {
  // Left arrow to cancel/skip update
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    logger.info("OTA: User declined update");
    ui.setInputText("");  // Clear the input text
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Right arrow or Enter to perform update if available
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    if (otaUpdater.getStatus() == UPDATE_AVAILABLE) {
      // User approved update
      keyboard.clearInput();
      logger.info("OTA: User approved update");
      appState = APP_OTA_UPDATING;
      ui.setState(STATE_OTA_UPDATE);
      ui.setInputText("Downloading...\nPlease wait");
      ui.update();
      
      // Perform update (blocking, will restart on success)
      otaUpdater.performUpdate();
      
      // If we get here, update failed
      ui.setInputText("Update Failed\nTry again later");
      ui.update();
      logger.error("OTA update failed");
      smartDelay(2000);
      
      ui.setInputText("");  // Clear the input text
      appState = APP_MAIN_MENU;
      ui.setState(STATE_VILLAGE_SELECT);
      ui.resetMenuSelection();
      ui.update();
    } else {
      // No update available, go back to main menu
      keyboard.clearInput();
      logger.info("OTA: No update available");
      ui.setInputText("");  // Clear the input text
      appState = APP_MAIN_MENU;
      ui.setState(STATE_VILLAGE_SELECT);
      ui.resetMenuSelection();
      ui.update();
    }
    smartDelay(300);
  }
}

void handleOTAUpdating() {
  // Just wait - update is in progress
  // Device will restart on success
  smartDelay(100);
}
