#include <Arduino.h>
#include <Wire.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include "version.h"
#include "Village.h"
#include "Encryption.h"
#include "MQTTMessenger.h"
#include "Keyboard.h"
#include "UI.h"
#include "Battery.h"
#include "Logger.h"
#include "WiFiManager.h"
#include "OTAUpdater.h"

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
#define BUZZER_PIN 40
#define BUZZER_CHANNEL 0

// Keyboard interrupt pin (same as I2C SDA - CardKB pulls LOW when key pressed)
#define KEYBOARD_INT_PIN 39

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
  APP_CONVERSATION_LIST,       // New: dynamic list of all valid villages
  APP_SETTINGS_MENU,
  APP_RINGTONE_SELECT,
  APP_WIFI_SETUP_MENU,
  APP_WIFI_NETWORK_LIST,      // New: show scanned networks
  APP_WIFI_NETWORK_OPTIONS,   // New: connect/forget for selected network
  APP_WIFI_NETWORK_DETAILS,   // New: show details of connected/saved network
  APP_WIFI_SAVED_NETWORKS,    // New: list all saved networks
  APP_WIFI_SSID_INPUT,
  APP_WIFI_PASSWORD_INPUT,
  APP_WIFI_CONNECTING,
  APP_WIFI_STATUS,
  APP_OTA_CHECKING,
  APP_OTA_UPDATING,
  APP_VILLAGE_MENU,
  APP_VILLAGE_CREATE,
  APP_VILLAGE_CREATED,
  APP_INVITE_EXPLAIN,
  APP_INVITE_CODE_DISPLAY,
  APP_JOIN_EXPLAIN,
  APP_JOIN_CODE_INPUT,
  APP_JOIN_USERNAME_INPUT,      // New: Enter username after invite join
  APP_VILLAGE_JOIN_NAME,
  APP_VILLAGE_JOIN_PASSWORD,
  APP_PASSWORD_INPUT,
  APP_USERNAME_INPUT,
  APP_VIEW_MEMBERS,
  APP_MESSAGING,
  APP_MESSAGE_COMPOSE
};

AppState appState = APP_MAIN_MENU;
AppState returnToState = APP_MAIN_MENU;  // Track where to return from invite flow
String messageComposingText = "";
String tempVillageName = "";  // Temp storage during village creation
String tempWiFiSSID = "";     // Temp storage during WiFi setup
String tempWiFiPassword = ""; // Temp storage during WiFi setup

// Conversation list tracking
struct ConversationEntry {
  int slot;
  String name;
  String id;
  unsigned long lastActivity;  // Timestamp of most recent message
};
std::vector<ConversationEntry> conversationList;

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

// Build conversation list from valid villages, sorted by most recent activity
void buildConversationList() {
  conversationList.clear();
  
  // Scan all 10 slots for valid villages
  for (int i = 0; i < 10; i++) {
    if (Village::hasVillageInSlot(i)) {
      ConversationEntry entry;
      entry.slot = i;
      entry.name = Village::getVillageNameFromSlot(i);
      entry.id = Village::getVillageIdFromSlot(i);
      entry.lastActivity = 0;  // TODO: get from most recent message timestamp
      conversationList.push_back(entry);
    }
  }
  
  // Sort by most recent activity (for now, just keep discovery order)
  // TODO: Sort by lastActivity timestamp once we track message times
  
  Serial.println("[Conversations] Found " + String(conversationList.size()) + " valid villages");
  for (const auto& conv : conversationList) {
    Serial.println("  Slot " + String(conv.slot) + ": " + conv.name);
  }
}

// Power management states
enum PowerMode {
  POWER_AWAKE,     // Active, polling, listening
  POWER_NAPPING,   // Deep sleep with 15-min wake cycles
  POWER_ASLEEP     // Low battery, stay asleep until charged
};

PowerMode powerMode = POWER_AWAKE;
unsigned long lastActivityTime = 0;  // Last message sent/received or user interaction
const unsigned long AWAKE_TIMEOUT = 300000;  // 5 minutes = 300,000ms
const unsigned long NAP_WAKE_INTERVAL = 900000;  // 15 minutes = 900,000ms
const float LOW_BATTERY_THRESHOLD = 3.0;  // 3.0V = too low, go to permanent sleep
float sleepBatteryVoltage = 0.0;  // Battery voltage when entering nap mode

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
  Serial.println("[Power] Entering deep sleep mode (mode=" + String(powerMode) + ")");
  
  // Check battery voltage before sleep
  float currentVoltage = battery.getVoltage();
  Serial.println("[Power] Battery voltage: " + String(currentVoltage) + "V");
  
  // If battery too low, enter permanent sleep mode
  if (currentVoltage < LOW_BATTERY_THRESHOLD) {
    Serial.println("[Power] Battery too low! Entering permanent sleep");
    logger.info("Power: Battery critical, permanent sleep");
    powerMode = POWER_ASLEEP;
    sleepBatteryVoltage = currentVoltage;
    
    // Show low battery screen
    ui.showLowBatteryScreen(currentVoltage);
    smartDelay(3000);
    
    Serial.println("[Power] Entering permanent sleep - charge to wake");
    Serial.flush();
    
    // No wake sources - only power cycle/reset will wake
    esp_deep_sleep_start();
    return;
  }
  
  // Allow MQTT to flush any pending messages
  if (mqttMessenger.isConnected()) {
    Serial.println("[Power] Flushing MQTT messages...");
    for (int i = 0; i < 10; i++) {
      mqttMessenger.loop();
      smartDelay(100);
    }
    Serial.println("[Power] MQTT messages flushed");
  }
  
  // For napping mode, show napping screen with battery info
  if (powerMode == POWER_NAPPING) {
    logger.info("Power: Entering nap mode");
    ui.showNappingScreen(currentVoltage, wifiManager.isConnected());
    smartDelay(2000);
  } else {
    // Manual sleep via Tab key
    logger.info("Entering deep sleep");
    ui.showPoweringDown();
    smartDelay(1000);
    ui.showSleepScreen();
    smartDelay(1000);
  }
  
  // Configure wake sources for napping mode
  if (powerMode == POWER_NAPPING) {
    // Wake on timer (15 minutes)
    esp_sleep_enable_timer_wakeup(NAP_WAKE_INTERVAL * 1000ULL);  // Convert ms to microseconds
    Serial.println("[Power] Timer wake enabled: 15 minutes");
    
    // Wake on keyboard interrupt (GPIO 39 = I2C SDA / CardKB INT)
    // CardKB pulls line LOW when any key is pressed
    esp_sleep_enable_ext0_wakeup((gpio_num_t)KEYBOARD_INT_PIN, 0);
    Serial.println("[Power] Keyboard wake enabled: GPIO 39 (any key press)");
  }
  
  Serial.println("[Power] Entering deep sleep now");
  Serial.flush();
  
  // Enter deep sleep
  esp_deep_sleep_start();
  
  // Device will restart from setup() when it wakes
}

// Forward declarations
void handleMainMenu();
void handleConversationList();
void handleSettingsMenu();
void handleRingtoneSelect();
void handleWiFiSetupMenu();
void handleWiFiNetworkList();
void handleWiFiSavedNetworks();
void handleWiFiNetworkOptions();
void handleWiFiSSIDInput();
void handleWiFiPasswordInput();
void handleWiFiConnecting();
void handleWiFiNetworkDetails();
void handleWiFiStatus();
void handleOTAChecking();
void handleOTAUpdating();
void handleVillageMenu();
void handleVillageCreate();
void handleVillageCreated();
void handleInviteExplain();
void handleInviteCodeDisplay();
void handleJoinExplain();
void handleJoinCodeInput();
void handleJoinUsernameInput();
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
  
  // Reset activity timer - new message keeps device awake
  lastActivityTime = millis();
  Serial.println("[Power] Activity timer reset - message received");
  
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
        ui.updateFull();
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

// Global variable to store pending invite data
struct PendingInvite {
  String villageId;
  String villageName;
  uint8_t encryptionKey[32];
  bool received = false;
} pendingInvite;

void onInviteReceived(const String& villageId, const String& villageName, const uint8_t* encryptedKey, size_t keyLen) {
  Serial.println("[Invite] Received invite data: " + villageName + " (" + villageId + ")");
  logger.info("Invite received: " + villageName);
  
  // Store invite data for processing in main loop
  pendingInvite.villageId = villageId;
  pendingInvite.villageName = villageName;
  memcpy(pendingInvite.encryptionKey, encryptedKey, 32);
  pendingInvite.received = true;
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
  
  // Check wake-up reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool wokeFromNap = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER || wakeup_reason == ESP_SLEEP_WAKEUP_EXT1);
  
  if (wokeFromNap) {
    Serial.println("[Power] Woke from nap - reason: " + String(wakeup_reason == ESP_SLEEP_WAKEUP_TIMER ? "TIMER" : "KEY_PRESS"));
  }
  
  // Initialize buzzer
  // First, explicitly detach GPIO 9 (old buzzer pin) to prevent dual activation
  ledcDetachPin(9);
  pinMode(9, INPUT);  // Set to high-impedance input to disable
  
  // Now set up the new buzzer on GPIO 40
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
  ui.updateFull();  // Full refresh at boot for clean initial display
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
  
  // Initialize power management - start awake timer
  powerMode = POWER_AWAKE;
  lastActivityTime = millis();
  Serial.println("[Power] Device awake - 5 minute activity timer started");
  
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
        mqttMessenger.setInviteCallback(onInviteReceived);
        
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
  
  // Timer wake logic will be handled after full initialization below
  
  // If woke from key press, stay awake and continue normal boot
  if (wokeFromNap && wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.println("[Power] Woke by key press - staying awake");
    Serial.println("[Display] Forcing full refresh after wake from nap");
    ui.updateClean();  // Force full display refresh to prevent white screen/corruption
    powerMode = POWER_AWAKE;
    lastActivityTime = millis();
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
      ui.updateFull();
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
  
  // If woke from nap timer (not key press), check for messages then go back to sleep
  if (wokeFromNap && wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("[Power] Nap timer wake - checking for new messages");
    
    // Try to reconnect WiFi if we have credentials but not connected
    if (wifiManager.hasCredentials() && !wifiManager.isConnected()) {
      Serial.println("[Power] WiFi disconnected - attempting reconnect...");
      if (wifiManager.connect()) {
        Serial.println("[Power] WiFi reconnected: " + wifiManager.getIPAddress());
        logger.info("WiFi auto-reconnect on wake: " + wifiManager.getIPAddress());
        
        // Reconnect MQTT if WiFi came back
        if (!mqttMessenger.isConnected()) {
          Serial.println("[Power] Reconnecting MQTT...");
          mqttMessenger.begin();
        }
      } else {
        Serial.println("[Power] WiFi reconnect failed");
      }
    }
    
    // Give more time for MQTT messages to arrive if WiFi is connected
    if (mqttMessenger.isConnected()) {
      Serial.println("[Power] Waiting for messages to arrive...");
      for (int i = 0; i < 30; i++) {  // Wait up to 3 seconds total
        mqttMessenger.loop();  // Process incoming messages
        smartDelay(100);
      }
      Serial.println("[Power] Message wait complete");
    }
    
    // Check if there are any unread messages (max 5 alerts)
    int unreadCount = 0;
    if (village.isInitialized()) {
      std::vector<Message> messages = village.loadMessages();
      for (const Message& msg : messages) {
        if (msg.status != MSG_READ && msg.status != MSG_SEEN) {
          unreadCount++;
        }
      }
    }
    
    if (unreadCount > 0) {
      // Show message notification screen
      Serial.println("[Power] Showing new message notification");
      ui.setState(STATE_SPLASH);  // Use splash state for simple text display
      String notificationText = "SmolTxt Napping\n\n";
      notificationText += "You have " + String(unreadCount) + " new message";
      if (unreadCount > 1) notificationText += "s";
      notificationText += "\n\nPress any key to view";
      ui.setInputText(notificationText);
      ui.updateClean();  // Show the notification
      
      int alertCount = min(unreadCount, 5);  // Max 5 ringtones
      Serial.println("[Power] Playing " + String(alertCount) + " alerts");
      for (int i = 0; i < alertCount; i++) {
        playRingtoneSound(selectedRingtone);
        smartDelay(1000);
      }
      
      // Keep message visible for a few seconds before sleeping
      Serial.println("[Power] Keeping notification visible");
      smartDelay(3000);
    } else {
      Serial.println("[Power] No new messages - staying asleep");
    }
    
    // Show napping screen before going back to sleep
    Serial.println("[Power] Showing napping screen");
    ui.showNappingScreen(battery.getVoltage(), wifiManager.isConnected());
    smartDelay(1000);
    
    // Go back to sleep
    Serial.println("[Power] Returning to nap mode");
    powerMode = POWER_NAPPING;
    enterDeepSleep();
    // Never returns - device enters deep sleep
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
    lastActivityTime = millis();  // Reset inactivity timer on any keyboard input
    // Serial.println("[Power] Activity timer reset - keyboard input");
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
    
    // Check if we've reached 3 seconds - trigger napping mode immediately while still holding
    if (holdDuration >= SHUTDOWN_HOLD_TIME && !isShuttingDown) {
      isShuttingDown = true;
      Serial.println("[Power] Manual nap triggered! (3s hold complete)");
      logger.info("Power: Entering nap mode (manual)");
      powerMode = POWER_NAPPING;
      sleepBatteryVoltage = battery.getVoltage();
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
    lastActivityTime = millis();  // Reset activity timer on message send
  }
  
  // Check for inactivity timeout - enter napping mode after 5 minutes
  if (powerMode == POWER_AWAKE) {
    unsigned long inactiveTime = millis() - lastActivityTime;
    if (inactiveTime >= AWAKE_TIMEOUT) {
      Serial.println("[Power] 5 minutes of inactivity - entering napping mode");
      logger.info("Power: Entering nap mode after inactivity");
      powerMode = POWER_NAPPING;
      sleepBatteryVoltage = battery.getVoltage();
      enterDeepSleep();
      // Never returns - device enters deep sleep
    }
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
    case APP_CONVERSATION_LIST:
      handleConversationList();
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
    case APP_WIFI_NETWORK_LIST:
      handleWiFiNetworkList();
      break;
    case APP_WIFI_SAVED_NETWORKS:
      handleWiFiSavedNetworks();
      break;
    case APP_WIFI_NETWORK_OPTIONS:
      handleWiFiNetworkOptions();
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
    case APP_WIFI_NETWORK_DETAILS:
      handleWiFiNetworkDetails();
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
    case APP_VILLAGE_CREATED:
      handleVillageCreated();
      break;
    case APP_INVITE_EXPLAIN:
      handleInviteExplain();
      break;
    case APP_INVITE_CODE_DISPLAY:
      handleInviteCodeDisplay();
      break;
    case APP_JOIN_EXPLAIN:
      handleJoinExplain();
      break;
    case APP_JOIN_CODE_INPUT:
      handleJoinCodeInput();
      break;
    case APP_JOIN_USERNAME_INPUT:
      handleJoinUsernameInput();
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
  // First time displaying main menu - use full refresh
  static bool firstMainMenuDisplay = true;
  if (firstMainMenuDisplay) {
    ui.updateFull();
    firstMainMenuDisplay = false;
  }
  
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
    
    // New simplified main menu:
    // 0: My Conversations
    // 1: New Village
    // 2: Join Village
    // 3: Settings
    
    if (selection == 0) {
      // My Conversations - build list and show it
      buildConversationList();
      keyboard.clearInput();
      appState = APP_CONVERSATION_LIST;
      ui.setState(STATE_CONVERSATION_LIST);
      ui.resetMenuSelection();
      ui.updateClean();
      Serial.println("[MainMenu] Opening conversation list");
    } else if (selection == 1) {
      // Selected "New Village" - ask for village name first
      isCreatingVillage = true;
      keyboard.clearInput();
      appState = APP_VILLAGE_CREATE;
      ui.setState(STATE_CREATE_VILLAGE);
      ui.setInputText("");
      ui.updateClean();  // Clean transition
    } else if (selection == 2) {
      // Selected "Join Conversation"
      isCreatingVillage = false;
      keyboard.clearInput();
      appState = APP_JOIN_EXPLAIN;
      ui.setState(STATE_JOIN_EXPLAIN);
      ui.resetMenuSelection();
      ui.updateClean();  // Clean transition
    } else if (selection == 3) {
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

void handleConversationList() {
  // Check for up/down navigation
  if (keyboard.isUpPressed()) {
    Serial.println("[ConversationList] UP pressed");
    ui.menuUp();
    ui.updatePartial();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    Serial.println("[ConversationList] DOWN pressed");
    ui.menuDown();
    ui.updatePartial();
    smartDelay(200);
  }
  
  // Backspace to delete selected conversation
  if (keyboard.isBackspacePressed()) {
    int selection = ui.getMenuSelection();
    if (selection >= 0 && selection < conversationList.size()) {
      ConversationEntry& entry = conversationList[selection];
      Serial.println("[ConversationList] BACKSPACE pressed - deleting village: " + entry.name);
      
      // Delete the village slot
      Village::deleteSlot(entry.slot);
      
      // Remove from MQTT subscriptions if it's the current village
      if (currentVillageSlot == entry.slot) {
        mqttMessenger.removeVillageSubscription(entry.id);
        village.clearVillage();
        currentVillageSlot = -1;
      }
      
      // FIXED: Clear stale conversation list and rebuild on next view
      conversationList.clear();
      
      // Return to main menu
      appState = APP_MAIN_MENU;
      ui.setState(STATE_MAIN_MENU);
      ui.resetMenuSelection();
      ui.updateClean();
      
      Serial.println("[ConversationList] Village deleted, returning to main menu");
    }
    smartDelay(300);
    return;
  }
  
  // Left arrow to go back to main menu
  if (keyboard.isLeftPressed()) {
    Serial.println("[ConversationList] LEFT pressed - back to main menu");
    keyboard.clearInput();
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // Enter or right arrow to select a conversation
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    
    if (selection >= 0 && selection < conversationList.size()) {
      // Load the selected village
      ConversationEntry& entry = conversationList[selection];
      Serial.println("[ConversationList] Loading village from slot " + String(entry.slot) + ": " + entry.name);
      
      if (village.loadFromSlot(entry.slot)) {
        currentVillageSlot = entry.slot;
        ui.setExistingVillageName(village.getVillageName());
        encryption.setKey(village.getEncryptionKey());
        
        // Set as active village for sending
        mqttMessenger.setActiveVillage(village.getVillageId());
        Serial.println("[ConversationList] Active village: " + village.getVillageName());
        
        // Go to village menu
        keyboard.clearInput();
        appState = APP_VILLAGE_MENU;
        ui.setState(STATE_VILLAGE_MENU);
        ui.resetMenuSelection();
        ui.updateClean();
      } else {
        Serial.println("[ConversationList] ERROR: Failed to load village from slot " + String(entry.slot));
      }
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
      ui.setInputText("");  // Clear any leftover text from other screens (WiFi details, etc.)
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
      std::vector<String> messagesToMarkRead;  // Batch list
      int unreadCount = 0;
      for (int i = startIndex; i < messages.size(); i++) {
        const Message& msg = messages[i];
        // Only process received messages that aren't already read
        if (msg.received && msg.status == MSG_RECEIVED && !msg.messageId.isEmpty()) {
          // Mark as read in UI
          ui.updateMessageStatus(msg.messageId, MSG_READ);
          
          // Add to batch list for storage update
          messagesToMarkRead.push_back(msg.messageId);
          
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
      
      // Batch update all messages at once (single file write instead of N writes)
      if (!messagesToMarkRead.empty()) {
        village.batchUpdateMessageStatus(messagesToMarkRead, MSG_READ);
        Serial.println("[App] Marked " + String(unreadCount) + " unread messages as read, queued receipts");
      }
      
      ui.update();  // Always refresh to show any messages received
    } else if (selection == 1) {
      // Invite a Friend - go to invite flow
      returnToState = APP_VILLAGE_MENU;
      appState = APP_INVITE_EXPLAIN;
      ui.setState(STATE_INVITE_EXPLAIN);
      ui.resetMenuSelection();
      ui.updateClean();  // Clean transition
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
        // Create new village with random UUID and encryption key
        Serial.println("[Create] Custom village name: " + tempVillageName);
        
        // Clear memory and create the new village with custom name
        village.clearVillage();
        village.createVillage(tempVillageName);
        ui.setExistingVillageName(tempVillageName);
        
        village.setUsername(currentName);
      } else {
        // FIXED: Joining via invite - just set username, village already loaded
        Serial.println("[Invite] Setting username after invite join: " + currentName);
        village.setUsername(currentName);
        
        // Send join announcement NOW that we have username
        String joinAnnouncement = currentName + " joined the conversation";
        String systemMsgId = mqttMessenger.sendSystemMessage(joinAnnouncement, "SmolTxt");
        if (systemMsgId.isEmpty()) {
          mqttMessenger.sendShout(joinAnnouncement);
        }
        logger.info("MQTT SYSTEM sent: " + joinAnnouncement);
      }
      // FIXED: Different flow for creators vs joiners
      if (!isCreatingVillage) {
        // JOINER PATH: Village already loaded and subscribed, just save username
        Serial.println("[Invite] Saving username to existing village slot " + String(currentVillageSlot));
        village.saveToSlot(currentVillageSlot);
        
        // Update MQTT subscription with new username
        mqttMessenger.addVillageSubscription(
          village.getVillageId(),
          village.getVillageName(),
          currentName,
          village.getEncryptionKey()
        );
        
        // Load messages and go to messaging screen (skip invite code flow continuation)
        ui.clearMessages();
        std::vector<Message> messages = village.loadMessages();
        int startIndex = messages.size() > MAX_MESSAGES_TO_LOAD ? messages.size() - MAX_MESSAGES_TO_LOAD : 0;
        for (int i = startIndex; i < messages.size(); i++) {
          ui.addMessage(messages[i]);
        }
        
        // Request sync
        unsigned long lastMsgTime = 0;
        for (const auto& msg : messages) {
          if (msg.timestamp > lastMsgTime) lastMsgTime = msg.timestamp;
        }
        if (mqttMessenger.isConnected()) {
          mqttMessenger.requestSync(lastMsgTime);
          smartDelay(500);
        }
        
        // Go to messaging
        keyboard.clearInput();
        ui.setInputText("");
        inMessagingScreen = true;
        lastMessagingActivity = millis();
        ui.setCurrentUsername(currentName);
        ui.resetMessageScroll();
        appState = APP_MESSAGING;
        ui.setState(STATE_MESSAGING);
        ui.update();
        return;  // Exit early for joiners
      }
      
      // CREATOR PATH: Need to find slot and set everything up
      village.setUsername(currentName);
      
      // Find appropriate slot for this village
      currentVillageSlot = -1;  // Reset slot search
      
      // Check if we somehow already created this village
      String villageId = village.getVillageId();
      currentVillageSlot = Village::findVillageSlotById(villageId);
      Serial.println("[Main] Creator checking for existing village ID: " + villageId + ", found in slot: " + String(currentVillageSlot));
      
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
        
        // Send initial message to the conversation from SmolTxt
        String username = village.getUsername();
        if (username.isEmpty()) {
          username = "Creator";
        }
        String joinMsg = username + " joined the conversation";
        
        // Send as system message from SmolTxt
        String systemMsgId = mqttMessenger.sendSystemMessage(joinMsg, "SmolTxt");
        if (systemMsgId.isEmpty()) {
          // Fallback if system message not available
          mqttMessenger.sendShout(joinMsg);
        }
        
        // Go to village created menu
        appState = APP_VILLAGE_CREATED;
        ui.setState(STATE_VILLAGE_CREATED);
        ui.resetMenuSelection();
        ui.update();
        smartDelay(300);
        return;
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
      ui.setInputText("");  // Clear any leftover text from other screens (WiFi details, etc.)
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
      std::vector<String> messagesToMarkRead;  // Batch list
      int unreadCount = 0;
      for (int i = startIndex; i < messages.size(); i++) {
        const Message& msg = messages[i];
        // Only process received messages that aren't already read
        if (msg.received && msg.status == MSG_RECEIVED && !msg.messageId.isEmpty()) {
          // Mark as read in UI
          ui.updateMessageStatus(msg.messageId, MSG_READ);
          
          // Add to batch list for storage update
          messagesToMarkRead.push_back(msg.messageId);
          
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
      
      // Batch update all messages at once (single file write instead of N writes)
      if (!messagesToMarkRead.empty()) {
        village.batchUpdateMessageStatus(messagesToMarkRead, MSG_READ);
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

void handleVillageCreated() {
  keyboard.update();
  
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    appState = APP_MESSAGING;
    inMessagingScreen = true;
    ui.setState(STATE_MESSAGING);
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Up/Down navigation
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updatePartial();
    smartDelay(150);
    return;
  }
  
  if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updatePartial();
    smartDelay(150);
    return;
  }
  
  // Enter to select
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    if (selection == 0) {  // Invite a Friend
      returnToState = APP_VILLAGE_CREATED;
      appState = APP_INVITE_EXPLAIN;
      ui.setState(STATE_INVITE_EXPLAIN);
      ui.resetMenuSelection();
      ui.update();
    } else if (selection == 1) {  // Back
      appState = APP_MESSAGING;
      inMessagingScreen = true;
      ui.setState(STATE_MESSAGING);
      ui.update();
    }
    smartDelay(300);
    return;
  }
}

void handleInviteExplain() {
  keyboard.update();
  
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    appState = returnToState;
    ui.setState(returnToState == APP_VILLAGE_MENU ? STATE_VILLAGE_MENU : STATE_VILLAGE_CREATED);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Up/Down navigation
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updatePartial();
    smartDelay(150);
    return;
  }
  
  if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updatePartial();
    smartDelay(150);
    return;
  }
  
  // Enter to select
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    if (selection == 0) {  // Generate A Code
      // Generate 8-digit code
      String code = String(random(10000000, 100000000));  // 8 digits: 10000000-99999999
      unsigned long expiry = millis() + 300000;  // 5 minutes from now
      ui.setInviteCode(code, expiry);
      
      // Publish invite code to MQTT
      Serial.println("[Invite] Publishing code: " + code);
      ui.showMessage("Publishing...", "Creating invite\ncode\n\nPlease wait...", 0);
      ui.update();
      
      if (mqttMessenger.publishInvite(code, village.getVillageId(), village.getVillageName(), village.getEncryptionKey())) {
        Serial.println("[Invite] Code published successfully");
        logger.info("Invite code published: " + code);
        smartDelay(300);  // Brief pause after publishing
      } else {
        Serial.println("[Invite] Failed to publish code");
        logger.error("Invite code publish failed");
      }
      
      appState = APP_INVITE_CODE_DISPLAY;
      ui.setState(STATE_INVITE_CODE_DISPLAY);
      ui.update();
    } else if (selection == 1) {  // Cancel
      appState = returnToState;
      ui.setState(returnToState == APP_VILLAGE_MENU ? STATE_VILLAGE_MENU : STATE_VILLAGE_CREATED);
      ui.resetMenuSelection();
      ui.update();
    }
    smartDelay(300);
    return;
  }
}

void handleInviteCodeDisplay() {
  keyboard.update();
  
  // Check if code expired
  if (millis() > ui.getInviteExpiry()) {
    String code = ui.getInviteCode();
    ui.clearInviteCode();
    
    // Unpublish invite code from MQTT to clear retained message
    mqttMessenger.unpublishInvite(code);
    
    String infoMsg = "The invite code has\nexpired.\n\nPress ENTER to continue";
    ui.showMessage("Code Expired", infoMsg, 0);
    while (!keyboard.isEnterPressed() && !keyboard.isRightPressed()) {
      keyboard.update();
      smartDelay(50);
    }
    
    appState = returnToState;
    ui.setState(returnToState == APP_VILLAGE_MENU ? STATE_VILLAGE_MENU : STATE_VILLAGE_CREATED);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Any key pressed - cancel and go back
  if (keyboard.hasInput() || keyboard.isEnterPressed() || keyboard.isLeftPressed()) {
    String code = ui.getInviteCode();
    ui.clearInviteCode();
    // Unsubscribe and unpublish invite
    if (!code.isEmpty()) {
      mqttMessenger.unsubscribeFromInvite(code);
      mqttMessenger.unpublishInvite(code);
    }
    
    appState = returnToState;
    ui.setState(returnToState == APP_VILLAGE_MENU ? STATE_VILLAGE_MENU : STATE_VILLAGE_CREATED);
    ui.resetMenuSelection();
    ui.update();
    keyboard.clearInput();
    smartDelay(300);
    return;
  }
  
  // Refresh display to update countdown timer every second
  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh > 1000) {
    ui.updatePartial();
    lastRefresh = millis();
  }
}

void handleJoinExplain() {
  keyboard.update();
  
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Up/Down navigation
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updatePartial();
    smartDelay(150);
    return;
  }
  
  if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updatePartial();
    smartDelay(150);
    return;
  }
  
  // Enter to select
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    if (selection == 0) {  // Enter a Code
      appState = APP_JOIN_CODE_INPUT;
      ui.setState(STATE_JOIN_CODE_INPUT);
      ui.setInputText("");
      ui.update();
    } else if (selection == 1) {  // Cancel
      appState = APP_MAIN_MENU;
      ui.setState(STATE_VILLAGE_SELECT);
      ui.resetMenuSelection();
      ui.update();
    }
    smartDelay(300);
    return;
  }
}

void handleJoinCodeInput() {
  keyboard.update();
  
  // Left arrow to go back to main menu
  if (keyboard.isLeftPressed()) {
    appState = APP_MAIN_MENU;
    ui.setState(STATE_MAIN_MENU);
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
  
  // Handle enter - validate code
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    String code = ui.getInputText();
    if (code.length() == 8) {
      Serial.println("[Invite] Attempting to join with code: " + code);
      logger.info("Join attempt with code: " + code);
      
      // Show verification screen with the code they entered
      String verifyMsg = "Checking code:\n" + code + "\n\nPlease wait...";
      ui.showMessage("Verifying", verifyMsg, 0);
      ui.update();
      smartDelay(500);  // Brief pause so user can verify the code
      
      // Reset invite flag BEFORE subscribing (callback could trigger immediately)
      pendingInvite.received = false;
      
      // Subscribe to invite topic
      if (mqttMessenger.subscribeToInvite(code)) {
        Serial.println("[Invite] Subscribed, waiting for invite data...");
        ui.showMessage("Looking up...", "Searching for\ninvite code\n\nThis may take a\nmoment...", 0);
        ui.update();
        
        // Wait up to 15 seconds for invite data
        unsigned long startWait = millis();
        while (!pendingInvite.received && (millis() - startWait < 15000)) {
          // Process WiFi and MQTT events to ensure callbacks are delivered
          mqttMessenger.loop();
          yield();  // Let FreeRTOS tasks run
          vTaskDelay(1);  // Allow MQTT task to process
          delay(50);  // Shorter delay for more responsive checking
        }
        
        // Unsubscribe from invite topic
        mqttMessenger.unsubscribeFromInvite(code);
        
        if (pendingInvite.received) {
          // Create village by manually building JSON and saving to slot
          Serial.println("[Invite] Creating village: " + pendingInvite.villageName);
          
          // Find available slot
          int slot = -1;
          for (int i = 0; i < 10; i++) {
            if (!Village::hasVillageInSlot(i)) {
              slot = i;
              break;
            }
          }
          
          if (slot >= 0) {
            // Build village JSON manually
            JsonDocument doc;
            doc["villageId"] = pendingInvite.villageId;
            doc["villageName"] = pendingInvite.villageName;
            doc["password"] = "invite-joined";  // Placeholder password
            doc["isOwner"] = false;
            doc["username"] = "member";  // FIXED: Use "username" not "myUsername" to match loadFromSlot()
            doc["initialized"] = true;  // Mark as initialized so loadMessages() works
            
            // FIXED: Convert encryption key to hex format (not base64) to match saveToSlot()
            String keyHex = "";
            for (int i = 0; i < 32; i++) {
              char hex[3];
              sprintf(hex, "%02x", pendingInvite.encryptionKey[i]);
              keyHex += hex;
            }
            doc["key"] = keyHex;  // Save as 64-char hex string
            
            // Save to file
            String filename = "/village_" + String(slot) + ".dat";
            File file = LittleFS.open(filename, "w");
            if (file) {
              serializeJson(doc, file);
              file.close();
              
              Serial.println("[Invite] Village saved to slot " + String(slot));
              logger.info("Joined village: " + pendingInvite.villageName);
              
              // Load the village
              if (village.loadFromSlot(slot)) {
                currentVillageSlot = slot;
                
                // Subscribe to village on MQTT
                mqttMessenger.addVillageSubscription(village.getVillageId(), village.getVillageName(), 
                                                    "member", village.getEncryptionKey());
                
                // Set as active village for sending messages
                mqttMessenger.setActiveVillage(village.getVillageId());
                ui.setExistingVillageName(village.getVillageName());
                encryption.setKey(village.getEncryptionKey());
                
                // Show success screen
                String successMsg = "Successfully\njoined:\n\n" + pendingInvite.villageName;
                ui.showMessage("Success!", successMsg, 0);
                ui.update();
                smartDelay(2000);  // Show success for 2 seconds
                
                // FIXED: Prompt for username before sending join announcement
                // Clear everything aggressively to prevent pre-filling
                keyboard.clearInput();
                ui.setInputText("");
                ui.setCurrentUsername("");  // Clear any cached username
                appState = APP_JOIN_USERNAME_INPUT;
                ui.setState(STATE_INPUT_USERNAME);
                ui.update();
                
                // Set flags for username handler to know we're joining (not creating)
                isCreatingVillage = false;
                tempVillageName = pendingInvite.villageName;  // Store for reference
              } else {
                Serial.println("[Invite] Failed to load village after save");
                ui.showMessage("Error", "Failed to load\nvillage data\n\nPress ENTER", 0);
                while (!keyboard.isEnterPressed()) { keyboard.update(); smartDelay(50); }
                appState = APP_MAIN_MENU;
                ui.setState(STATE_MAIN_MENU);
                ui.resetMenuSelection();
                ui.update();
              }
            } else {
              Serial.println("[Invite] Failed to save village");
              ui.showMessage("Error", "Failed to save\nvillage data\n\nPress ENTER", 0);
              while (!keyboard.isEnterPressed()) { keyboard.update(); smartDelay(50); }
              appState = APP_MAIN_MENU;
              ui.setState(STATE_MAIN_MENU);
              ui.resetMenuSelection();
              ui.update();
            }
          } else {
            Serial.println("[Invite] No available slots");
            ui.showMessage("Error", "No available slots\n(max 10 conversations)\n\nPress ENTER", 0);
            while (!keyboard.isEnterPressed()) { keyboard.update(); smartDelay(50); }
            appState = APP_MAIN_MENU;
            ui.setState(STATE_MAIN_MENU);
            ui.resetMenuSelection();
            ui.update();
          }
          
          pendingInvite.received = false;
        } else {
          Serial.println("[Invite] Timeout waiting for invite data");
          logger.error("Invite code timeout: " + code);
          String errorMsg = "Code not found\nor has expired\n\nCheck the code\nand try again\n\nPress ENTER";
          ui.showMessage("Not Found", errorMsg, 0);
          while (!keyboard.isEnterPressed()) { keyboard.update(); smartDelay(50); }
          appState = APP_MAIN_MENU;
          ui.setState(STATE_MAIN_MENU);
          ui.resetMenuSelection();
          ui.update();
        }
      } else {
        Serial.println("[Invite] Failed to subscribe to invite topic");
        ui.showMessage("Error", "Network error\n\nPlease try again\n\nPress ENTER", 0);
        while (!keyboard.isEnterPressed()) { keyboard.update(); smartDelay(50); }
        appState = APP_MAIN_MENU;
        ui.setState(STATE_MAIN_MENU);
        ui.resetMenuSelection();
        ui.update();
      }
    }
    smartDelay(300);
    return;
  }
  
  // Only accept numeric input, max 8 digits
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    for (char c : input) {
      if (c >= '0' && c <= '9' && ui.getInputText().length() < 8) {
        ui.addInputChar(c);
      }
    }
    keyboard.clearInput();
    ui.updatePartial();
  }
}

void handleJoinUsernameInput() {
  keyboard.update();
  
  // Left arrow returns to main menu
  if (keyboard.isLeftPressed()) {
    appState = APP_MAIN_MENU;
    ui.setState(STATE_MAIN_MENU);
    ui.resetMenuSelection();
    ui.setInputText("");
    keyboard.clearInput();
    ui.update();
    smartDelay(300);
    return;
  }
  
  // Check for ENTER - save username and send join announcement
  if (keyboard.isEnterPressed()) {
    String currentName = ui.getInputText();
    currentName.trim();
    
    if (currentName.length() > 0) {
      // Update village with actual username
      village.setUsername(currentName);
      village.saveToSlot(currentVillageSlot);
      
      // Re-subscribe to village with updated username
      mqttMessenger.addVillageSubscription(village.getVillageId(), village.getVillageName(), 
                                          currentName, village.getEncryptionKey());
      
      // Send join announcement with actual username
      String announcement = currentName + " joined the conversation";
      mqttMessenger.sendSystemMessage(announcement, "SmolTxt");
      logger.info("User joined: " + currentName);
      
      // Request message sync to get creator's join message and any other history
      if (mqttMessenger.isConnected()) {
        mqttMessenger.requestSync(0);  // Request all messages from the beginning
        smartDelay(500);  // Give time for sync to arrive
      }
      
      // Transition to messaging screen
      appState = APP_MESSAGING;
      ui.setState(STATE_MESSAGING);
      ui.setCurrentUsername(currentName);
      ui.setInputText("");
      keyboard.clearInput();
      ui.update();
      inMessagingScreen = true;
      lastMessagingActivity = millis();
      
      pendingInvite.received = false;  // Clear invite flag
    }
    smartDelay(300);
    return;
  }
  
  // Regular text input
  if (keyboard.hasInput()) {
    String input = keyboard.getInput();
    for (char c : input) {
      ui.addInputChar(c);
    }
    keyboard.clearInput();
    ui.updatePartial();
  }
  
  // Backspace handling
  if (keyboard.isBackspacePressed()) {
    ui.removeInputChar();
    keyboard.clearInput();
    ui.updatePartial();
    smartDelay(100);
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
      
      // Update WiFi connection status in UI
      if (wifiManager.isConnected()) {
        ui.setWiFiConnected(true, wifiManager.getConnectedSSID());
      } else {
        ui.setWiFiConnected(false, "");
      }
      ui.setSavedNetworkCount(wifiManager.getSavedNetworkCount());
      
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
      ui.updateFull();
      
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
    int savedCount = wifiManager.getSavedNetworkCount();
    
    // If we have saved networks: item 0 = Network Details or Saved Networks, item 1 = Scan Networks
    // If no saved networks: item 0 = Scan Networks
    
    if (savedCount > 0 && selection == 0) {
      keyboard.clearInput();
      
      if (savedCount == 1) {
        // Single saved network - go directly to Network Details
        appState = APP_WIFI_NETWORK_DETAILS;
        ui.setState(STATE_WIFI_NETWORK_DETAILS);
        
        // Get the saved network
        auto savedNets = wifiManager.getSavedNetworks();
        String savedSSID = savedNets[0].ssid;
        bool isConnected = wifiManager.isConnected() && wifiManager.getConnectedSSID() == savedSSID;
        
        // Format network details
        String details = "IP Address\n";
        details += isConnected ? wifiManager.getIPAddress() : "---\n";
        details += "Signal\n";
        details += isConnected ? (String(wifiManager.getSignalStrength()) + " dBm") : "---";
        ui.setInputText(details);
        ui.setConnectedSSID(savedSSID);
        ui.updateClean();
      } else {
        // Multiple saved networks - show list
        appState = APP_WIFI_SAVED_NETWORKS;
        ui.setState(STATE_WIFI_SAVED_NETWORKS);
        
        // Build network list for UI
        auto savedNets = wifiManager.getSavedNetworks();
        std::vector<String> ssids;
        std::vector<int> rssis;
        std::vector<bool> encrypted;
        std::vector<bool> saved;
        
        for (const auto& net : savedNets) {
          ssids.push_back(net.ssid);
          // Check if this network is currently connected
          bool isConnected = wifiManager.isConnected() && wifiManager.getConnectedSSID() == net.ssid;
          rssis.push_back(isConnected ? wifiManager.getSignalStrength() : -999);  // -999 = inactive
          encrypted.push_back(true);  // All saved networks have passwords
          saved.push_back(true);
        }
        
        ui.setNetworkList(ssids, rssis, encrypted, saved);
        ui.resetMenuSelection();
        ui.updateClean();
      }
    } else if ((savedCount > 0 && selection == 1) || (savedCount == 0 && selection == 0)) {
      // Scan for networks
      Serial.println("[WiFi] Scanning for networks...");
      ui.showMessage("WiFi", "Scanning...", 1000);
      
      auto networks = wifiManager.scanNetworks();
      
      // Convert to vectors for UI
      std::vector<String> ssids;
      std::vector<int> rssis;
      std::vector<bool> encrypted;
      std::vector<bool> saved;
      
      for (const auto& net : networks) {
        ssids.push_back(net.ssid);
        rssis.push_back(net.rssi);
        encrypted.push_back(net.encrypted);
        saved.push_back(net.saved);
      }
      
      ui.setNetworkList(ssids, rssis, encrypted, saved);
      
      keyboard.clearInput();
      appState = APP_WIFI_NETWORK_LIST;
      ui.setState(STATE_WIFI_NETWORK_LIST);
      ui.resetMenuSelection();
      ui.updateClean();
    }
    
    smartDelay(300);
  }
}

void handleWiFiNetworkList() {
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updateClean();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updateClean();
    smartDelay(200);
  }
  
  // Left arrow to go back to WiFi menu
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_SETUP_MENU;
    ui.setState(STATE_WIFI_SETUP_MENU);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // RIGHT/ENTER to select network
  if (keyboard.isRightPressed() || keyboard.isEnterPressed()) {
    int selection = ui.getMenuSelection();
    String selectedSSID = ui.getNetworkSSID(selection);
    
    if (selectedSSID.length() > 0) {
      tempWiFiSSID = selectedSSID;
      
      // Check if this is the currently connected network
      if (wifiManager.isConnected() && selectedSSID == wifiManager.getConnectedSSID()) {
        // Show network details
        keyboard.clearInput();
        appState = APP_WIFI_NETWORK_DETAILS;
        ui.setState(STATE_WIFI_NETWORK_DETAILS);
        
        // Format network details
        String details = "IP Address\\n";
        details += wifiManager.getIPAddress() + "\\n";
        details += "Signal\\n";
        details += String(wifiManager.getSignalStrength()) + " dBm";
        ui.setInputText(details);
        ui.updateClean();
      } else if (wifiManager.hasNetwork(selectedSSID)) {
        // Show connect/forget options
        keyboard.clearInput();
        appState = APP_WIFI_NETWORK_OPTIONS;
        ui.setState(STATE_WIFI_NETWORK_OPTIONS);
        ui.updateClean();
      } else {
        // New network - go to password input
        keyboard.clearInput();
        appState = APP_WIFI_PASSWORD_INPUT;
        ui.setState(STATE_WIFI_PASSWORD_INPUT);
        ui.setInputText("");
        ui.update();
      }
    }
    
    smartDelay(300);
  }
}

void handleWiFiSavedNetworks() {
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updateClean();
    smartDelay(200);
  } else if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updateClean();
    smartDelay(200);
  }
  
  // Left arrow to go back to WiFi menu
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_SETUP_MENU;
    ui.setState(STATE_WIFI_SETUP_MENU);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // Right arrow to view network details
  if (keyboard.isRightPressed()) {
    int selection = ui.getMenuSelection();
    String selectedSSID = ui.getNetworkSSID(selection);
    
    if (selectedSSID.length() > 0) {
      keyboard.clearInput();
      appState = APP_WIFI_NETWORK_DETAILS;
      ui.setState(STATE_WIFI_NETWORK_DETAILS);
      
      // Check if this network is currently connected
      bool isConnected = wifiManager.isConnected() && selectedSSID == wifiManager.getConnectedSSID();
      
      // Format network details
      String details = "IP Address\n";
      details += isConnected ? wifiManager.getIPAddress() : "---\n";
      details += "Signal\n";
      details += isConnected ? (String(wifiManager.getSignalStrength()) + " dBm") : "---";
      ui.setInputText(details);
      ui.setConnectedSSID(selectedSSID);
      ui.setNetworkActive(isConnected);
      ui.updateClean();
    }
    
    smartDelay(300);
  }
}

void handleWiFiNetworkOptions() {
  // LEFT to go back
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_NETWORK_LIST;
    ui.setState(STATE_WIFI_NETWORK_LIST);
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // Show simple menu - just text for now
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    // For saved networks: option 0 = connect, option 1 = forget
    // Simplified: ENTER to connect, BACKSPACE to forget
  }
  
  // ENTER/RIGHT to connect
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
    Serial.println("[WiFi] Connecting to: " + tempWiFiSSID);
    ui.showMessage("WiFi", "Connecting...", 1000);
    
    if (wifiManager.connectToNetwork(tempWiFiSSID)) {
      logger.info("WiFi connected: " + tempWiFiSSID);
      ui.showMessage("WiFi", "Connected!", 2000);
      
      // Reconnect MQTT if needed
      if (!mqttMessenger.isConnected()) {
        mqttMessenger.begin();
      }
    } else {
      ui.showMessage("WiFi", "Connection failed", 2000);
    }
    
    keyboard.clearInput();
    appState = APP_WIFI_SETUP_MENU;
    ui.setState(STATE_WIFI_SETUP_MENU);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // BACKSPACE to forget network
  if (keyboard.isBackspacePressed()) {
    Serial.println("[WiFi] Forgetting network: " + tempWiFiSSID);
    wifiManager.removeNetwork(tempWiFiSSID);
    logger.info("WiFi network forgotten: " + tempWiFiSSID);
    ui.showMessage("WiFi", "Network forgotten", 1500);
    
    keyboard.clearInput();
    appState = APP_WIFI_NETWORK_LIST;
    ui.setState(STATE_WIFI_NETWORK_LIST);
    ui.resetMenuSelection();
    ui.updateClean();
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
      // Check if network limit is reached (only for new networks)
      if (!wifiManager.hasNetwork(tempWiFiSSID) && wifiManager.getSavedNetworkCount() >= 10) {
        // Show warning message
        ui.showMessage("WiFi", "Max networks reached\nForget a network first", 3000);
        smartDelay(3000);
        
        // Go back to network list
        keyboard.clearInput();
        appState = APP_WIFI_NETWORK_LIST;
        ui.setState(STATE_WIFI_NETWORK_LIST);
        ui.resetMenuSelection();
        ui.updateClean();
        return;
      }
      
      // Show connecting message
      ui.showMessage("WiFi", "Connecting...", 1000);
      
      // Attempt connection
      if (wifiManager.connectWithCredentials(tempWiFiSSID, tempWiFiPassword)) {
        // Save network only after successful connection
        wifiManager.saveNetwork(tempWiFiSSID, tempWiFiPassword);
        logger.info("WiFi connected and saved: " + tempWiFiSSID);
        ui.showMessage("WiFi", "Connected!", 2000);
        
        // Reconnect MQTT if needed
        if (!mqttMessenger.isConnected()) {
          mqttMessenger.begin();
        }
      } else {
        ui.showMessage("WiFi", "Connection failed", 2000);
        logger.error("WiFi connection failed");
      }
      
      // Go back to network list
      keyboard.clearInput();
      appState = APP_WIFI_NETWORK_LIST;
      ui.setState(STATE_WIFI_NETWORK_LIST);
      ui.resetMenuSelection();
      ui.updateClean();
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

void handleWiFiNetworkDetails() {
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_WIFI_SETUP_MENU;
    ui.setState(STATE_WIFI_SETUP_MENU);
    ui.resetMenuSelection();
    ui.updateClean();
    smartDelay(300);
    return;
  }
  
  // UP/DOWN navigation
  if (keyboard.isUpPressed()) {
    ui.menuUp();
    ui.updateClean();
    smartDelay(200);
    return;
  } else if (keyboard.isDownPressed()) {
    ui.menuDown();
    ui.updateClean();
    smartDelay(200);
    return;
  }
  
  // RIGHT/ENTER to select option
  if (keyboard.isRightPressed() || keyboard.isEnterPressed()) {
    String ssid = ui.getConnectedSSID();
    bool isActive = ui.getNetworkActive();
    int selection = ui.getMenuSelection();
    
    // If network is not active, first menu item is "Join Network"
    if (!isActive && selection == 0) {
      // Join this network
      Serial.println("[WiFi] Joining saved network: " + ssid);
      
      ui.showMessage("WiFi", "Connecting to\n" + ssid, 0);
      smartDelay(500);
      
      bool success = wifiManager.connectToNetwork(ssid);
      
      if (success) {
        ui.showMessage("WiFi", "Connected!", 2000);
      } else {
        ui.showMessage("WiFi", "Connection failed", 2000);
      }
      
      keyboard.clearInput();
      appState = APP_WIFI_SETUP_MENU;
      ui.setState(STATE_WIFI_SETUP_MENU);
      ui.resetMenuSelection();
      smartDelay(2000);
      ui.updateClean();
    }
    // Forget Network option (menu item 0 if active, 1 if not active)
    else if ((isActive && selection == 0) || (!isActive && selection == 1)) {
      Serial.println("[WiFi] Forgetting network: " + ssid);
      
      // Disconnect if it's the currently connected network
      if (wifiManager.isConnected() && wifiManager.getConnectedSSID() == ssid) {
        wifiManager.disconnect();
      }
      
      // Remove from saved networks
      wifiManager.removeNetwork(ssid);
      
      ui.showMessage("WiFi", "Network forgotten", 2000);
      
      keyboard.clearInput();
      appState = APP_WIFI_SETUP_MENU;
      ui.setState(STATE_WIFI_SETUP_MENU);
      ui.resetMenuSelection();
      smartDelay(2000);
      ui.updateClean();
    }
    smartDelay(300);
  }
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
