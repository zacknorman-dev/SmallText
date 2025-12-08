#include <Arduino.h>
#include <Wire.h>
#include "Village.h"
#include "Encryption.h"
#include "LoRaMessenger.h"
#include "MQTTMessenger.h"
#include "Keyboard.h"
#include "UI.h"
#include "Battery.h"
#include "Logger.h"
#include "WiFiManager.h"
#include "OTAUpdater.h"

#define BUILD_NUMBER "v0.33.3"

// Pin definitions for Heltec Vision Master E290
#define LORA_CS 8
#define LORA_DIO1 14
#define LORA_RST 12
#define LORA_BUSY 13

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

// Global objects
Village village;
Encryption encryption;
LoRaMessenger messenger;
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
unsigned long lastRadioTransmission = 0;  // Track when radio last transmitted (for timing read receipts)
unsigned long lastVillageNameRequest = 0;  // Track pending village name requests (retry every 30s)
unsigned long lastOTACheck = 0;  // Track automatic OTA update checks

// Timestamp baseline to ensure new messages after reboot sort correctly
unsigned long timestampBaseline = 0;

// Forward declarations
void handleMainMenu();
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

// Message callback
void onMessageReceived(const Message& msg) {
  Serial.println("[Message] From " + msg.sender + ": " + msg.content);
  
  // Ensure received message timestamp is greater than stored messages
  Message adjustedMsg = msg;
  adjustedMsg.timestamp = max(msg.timestamp, max(millis(), timestampBaseline + 1));
  timestampBaseline = adjustedMsg.timestamp;  // Update baseline
  
  ui.addMessage(adjustedMsg);
  village.saveMessage(adjustedMsg);
  Serial.println("[Message] Total messages in history: " + String(ui.getMessageCount()));
  
  // Only mark as read if this is a NEW message (not a synced historical message)
  // Synced messages have MSG_RECEIVED status and should keep that status
  if (appState == APP_MESSAGING && inMessagingScreen && adjustedMsg.status != MSG_RECEIVED) {
    Serial.println("[App] Already in messaging screen, marking NEW message as read");
    
    // Mark message as read locally
    adjustedMsg.status = MSG_READ;
    ui.updateMessageStatus(adjustedMsg.messageId, MSG_READ);
    village.updateMessageStatus(adjustedMsg.messageId, MSG_READ);
    
    // Mark that radio just transmitted (the ACK)
    lastRadioTransmission = millis();
    
    // Queue read receipt for background sending (don't send immediately - radio may be busy)
    if (!adjustedMsg.senderMAC.isEmpty()) {
      ReadReceiptQueueItem item;
      item.messageId = adjustedMsg.messageId;
      item.recipientMAC = adjustedMsg.senderMAC;
      readReceiptQueue.push_back(item);
      Serial.println("[App] Queued immediate read receipt for: " + adjustedMsg.messageId);
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
  ui.updateMessageStatus(messageId, MSG_RECEIVED);
  village.updateMessageStatus(messageId, MSG_RECEIVED);  // Persist to storage
  
  // Update display if we're actively viewing messaging screen
  if (inMessagingScreen) {
    ui.updatePartial();
  }
}

void onMessageReadReceipt(const String& messageId, const String& fromMAC) {
  Serial.println("[Message] Read receipt for: " + messageId + " from " + fromMAC);
  ui.updateMessageStatus(messageId, MSG_READ);
  village.updateMessageStatus(messageId, MSG_READ);  // Persist to storage
  
  // Update display if we're actively viewing messaging screen
  if (inMessagingScreen) {
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
  } else {
    Serial.println("[Command] Unknown command: " + command);
    logger.error("Unknown command: " + command);
  }
}

// Handle sync request from other device
void onSyncRequest(const String& requestorMAC, unsigned long requestedTimestamp) {
  Serial.println("[Sync] Request from " + requestorMAC + " (ignoring timestamp - will send all messages)");
  logger.info("Sync request from " + requestorMAC);
  
  // Load ALL our messages - deduplication will happen on receiving device
  // Can't use timestamp comparison because millis() is device-specific
  std::vector<Message> allMessages = village.loadMessages();
  
  // Filter out messages without IDs (from old builds) - can't be deduplicated
  std::vector<Message> validMessages;
  for (const Message& msg : allMessages) {
    if (!msg.messageId.isEmpty()) {
      validMessages.push_back(msg);
    }
  }
  
  if (validMessages.empty()) {
    Serial.println("[Sync] No valid messages to send");
    logger.info("Sync: No messages with IDs");
    return;
  }
  
  Serial.println("[Sync] Sending " + String(validMessages.size()) + " messages (filtered " + String(allMessages.size() - validMessages.size()) + " without IDs) to " + requestorMAC);
  logger.info("Sync: Sending " + String(validMessages.size()) + " msgs");
  mqttMessenger.sendSyncResponse(requestorMAC, validMessages);
}

void onVillageNameReceived(const String& villageName) {
  // Check if this is a REQUEST signal (owner should respond)
  if (villageName == "REQUEST") {
    Serial.println("[Village] Received name request");
    if (village.amOwner()) {
      Serial.println("[Village] We are owner, sending announcement");
      messenger.sendVillageNameAnnouncement();
    }
    return;
  }
  
  Serial.println("[Village] Received village name announcement: " + villageName);
  
  // Update the village name and save to current slot
  village.setVillageName(villageName);
  if (currentVillageSlot >= 0 && currentVillageSlot < 10) {
    village.saveToSlot(currentVillageSlot);
    Serial.println("[Village] Updated name in slot " + String(currentVillageSlot));
  }
  
  // Update UI with new village name
  ui.setExistingVillageName(villageName);
  ui.update();  // Force full update to show new name
  
  // Update MQTT messenger with new village name
  // messenger.setVillageInfo(village.getVillageId(), villageName, village.getUsername());  // LoRa disabled
  mqttMessenger.setVillageInfo(village.getVillageId(), villageName, village.getUsername());
}

void setup() {
  // Enable Vext power for peripherals
  pinMode(18, OUTPUT);
  digitalWrite(18, HIGH);
  smartDelay(100);
  
  Serial.begin(115200);
  smartDelay(1000);
  
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
  
  // LoRa disabled - using MQTT only for faster messaging
  Serial.println("[LoRa] LoRa disabled - MQTT-only mode");
  // if (!messenger.begin(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY)) {
  //   logger.critical("LoRa radio initialization failed");
  //   Serial.println("[LoRa] WARNING - Failed to initialize!");
  //   Serial.println("[LoRa] Continuing without radio...");
  // } else {
  //   logger.info("LoRa radio initialized successfully");
  //   Serial.println("[LoRa] Success! Radio ready");
  // }
  Serial.flush();
  smartDelay(100);
  
  // Initialize display AFTER LoRa (will reconfigure SPI for e-paper)
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
  ui.update();
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
  
  // LoRa callbacks disabled - MQTT only
  // messenger.setMessageCallback(onMessageReceived);
  // messenger.setAckCallback(onMessageAcked);
  // messenger.setReadCallback(onMessageReadReceipt);
  // messenger.setVillageNameCallback(onVillageNameReceived);
  
  // Initialize messaging screen flag
  inMessagingScreen = false;
  lastMessagingActivity = 0;
  currentVillageSlot = -1;  // No village loaded yet
  
  // Don't auto-load any village - let user select from menu
  
  // Set delay callback for responsive keyboard during LoRa transmissions
  // messenger.setDelayCallback(smartDelay);  // LoRa disabled
  
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
        
        // Set up MQTT callbacks (same as LoRa)
        mqttMessenger.setMessageCallback(onMessageReceived);
        mqttMessenger.setAckCallback(onMessageAcked);
        mqttMessenger.setReadCallback(onMessageReadReceipt);
        mqttMessenger.setCommandCallback(onCommandReceived);
        mqttMessenger.setSyncRequestCallback(onSyncRequest);
        
        // Set encryption
        mqttMessenger.setEncryption(&encryption);
        
        // Enable MQTT debug logging
        logger.setMQTTClient(mqttMessenger.getClient());
      } else {
        Serial.println("[MQTT] Failed to initialize");
      }
    }
  } else {
    Serial.println("[WiFi] No saved WiFi credentials");
  }
  
  // Initialize timestamp baseline from existing messages in storage
  std::vector<Message> existingMessages = village.loadMessages();
  unsigned long lastMessageTimestamp = 0;
  
  for (const auto& msg : existingMessages) {
    if (msg.timestamp > timestampBaseline) {
      timestampBaseline = msg.timestamp;
    }
    if (msg.timestamp > lastMessageTimestamp) {
      lastMessageTimestamp = msg.timestamp;
    }
  }
  Serial.print("[Setup] Timestamp baseline initialized: ");
  Serial.println(timestampBaseline);
  
  // Rebuild message ID cache for deduplication
  village.rebuildMessageIdCache();
  
  // Request message sync if we have MQTT connection (in case we missed messages while offline)
  if (mqttMessenger.isConnected() && lastMessageTimestamp > 0) {
    Serial.println("[Sync] Requesting messages newer than " + String(lastMessageTimestamp));
    mqttMessenger.requestSync(lastMessageTimestamp);
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
  appState = APP_MAIN_MENU;
  ui.setState(STATE_VILLAGE_SELECT);
  ui.resetMenuSelection();
  Serial.println("[System] About to call ui.update() for village select...");
  ui.update();
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
  
  // Process incoming messages via MQTT only
  // messenger.loop();  // LoRa disabled
  
  // Process MQTT messages if connected
  mqttMessenger.loop();
  
  // Update battery readings (rate-limited internally)
  battery.update();
  ui.setBatteryStatus(battery.getVoltage(), battery.getPercent());
  
  // Process read receipt queue in background (send one per loop iteration)
  static unsigned long lastReadReceiptSent = 0;
  if (!readReceiptQueue.empty() && (millis() - lastRadioTransmission > 150) && (millis() - lastReadReceiptSent > 150)) {
    ReadReceiptQueueItem item = readReceiptQueue.front();
    readReceiptQueue.erase(readReceiptQueue.begin());
    
    // Send read receipt via MQTT only
    // LoRaMessenger::clearReceivedFlag();  // LoRa disabled
    
    Serial.println("[App] Sending queued read receipt for: " + item.messageId);
    mqttMessenger.sendReadReceipt(item.messageId, item.recipientMAC);
    
    lastReadReceiptSent = millis();
    lastRadioTransmission = millis();
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
  
  // Backspace to delete selected village
  if (keyboard.isBackspacePressed()) {
    int selection = ui.getMenuSelection();
    
    // Count existing villages
    int villageCount = 0;
    int villageSlots[10];
    for (int i = 0; i < 10; i++) {
      if (Village::hasVillageInSlot(i)) {
        villageSlots[villageCount] = i;
        villageCount++;
      }
    }
    
    // Only delete if selecting an existing village (not "New" or "Join")
    if (selection < villageCount) {
      int slot = villageSlots[selection];
      String villageName = Village::getVillageNameFromSlot(slot);
      
      Serial.println("[MainMenu] Deleting village from slot " + String(slot));
      Village::deleteSlot(slot);
      
      // Show confirmation
      ui.showMessage("Village Deleted", villageName + " removed", 1000);
      
      // Reset selection and refresh
      ui.resetMenuSelection();
      ui.setState(STATE_VILLAGE_SELECT);
      ui.update();
    }
    
    smartDelay(300);
    return;
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
        // messenger.setEncryption(&encryption);  // LoRa disabled
        // messenger.setVillageInfo(village.getVillageId(), village.getVillageName(), village.getUsername());  // LoRa disabled
        
        // Configure MQTT (set even if not connected yet)
        mqttMessenger.setEncryption(&encryption);
        mqttMessenger.setVillageInfo(village.getVillageId(), village.getVillageName(), village.getUsername());
        
        // Go to village menu
        keyboard.clearInput();
        appState = APP_VILLAGE_MENU;
        ui.setState(STATE_VILLAGE_MENU);
        ui.resetMenuSelection();
        ui.update();
      }
    } else if (selection == villageCount) {
      // Selected "New Village" - ask for village name first
      isCreatingVillage = true;
      keyboard.clearInput();
      appState = APP_VILLAGE_CREATE;
      ui.setState(STATE_CREATE_VILLAGE);
      ui.setInputText("");
      ui.update();
    } else if (selection == villageCount + 1) {
      // Selected "Join Village"
      isCreatingVillage = false;
      keyboard.clearInput();
      appState = APP_VILLAGE_JOIN_PASSWORD;
      ui.setState(STATE_JOIN_VILLAGE_PASSWORD);
      ui.setInputText("");
      ui.update();
    } else if (selection == villageCount + 2) {
      // Selected "WiFi & Updates"
      keyboard.clearInput();
      appState = APP_WIFI_SETUP_MENU;
      ui.setState(STATE_WIFI_SETUP_MENU);
      ui.resetMenuSelection();
      ui.update();
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
    ui.update();
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
      
      // If we're the owner, send village name announcement for joiners
      if (village.amOwner()) {
        messenger.sendVillageNameAnnouncement();
        Serial.println("[Village] Sent village name announcement");
      }
      // If we're a joiner with "Pending..." name, request the real name
      else if (village.getVillageName() == "Pending...") {
        messenger.sendVillageNameRequest();
        Serial.println("[Village] Requested village name from owner");
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
      
      // DON'T queue read receipts for old messages - they were already read in previous session
      // Read receipts only get sent when NEW messages arrive while in messaging screen
      // DON'T queue read receipts for old messages - they were already read in previous session
      // Read receipts only get sent when NEW messages arrive while in messaging screen
      // This prevents blocking on startup when loading historical messages
      readReceiptQueue.clear();  // Clear any old queue items
      Serial.println("[App] Not queuing read receipts for historical messages");
      
      ui.update();  // Always refresh to show any messages received
    } else if (selection == 1) {
      // Add Member (owner only)
      // TODO: Implement add member screen
    } else if (selection == 2) {
      // View Members
      appState = APP_VIEW_MEMBERS;
      ui.setState(STATE_VIEW_MEMBERS);
      ui.setMemberList(village.getMemberList());
      ui.update();
    } else if (selection == 3) {
      // Leave Village
      ui.showMessage("Leave Village?", "Press ENTER to confirm\nor LEFT to cancel", 0);
      
      while (true) {
        keyboard.update();
        if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
          // Confirmed - delete village slot
          if (currentVillageSlot >= 0) {
            Village::deleteSlot(currentVillageSlot);
          }
          village.clearVillage();
          currentVillageSlot = -1;
          
          ui.showMessage("Village", "Left village", 1500);
          smartDelay(1500);
          
          appState = APP_MAIN_MENU;
          ui.setState(STATE_VILLAGE_SELECT);
          ui.resetMenuSelection();
          ui.update();
          break;
        } else if (keyboard.isLeftPressed()) {
          ui.setState(STATE_VILLAGE_MENU);
          ui.update();
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
    ui.update();
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
      
      // Joiner starts with placeholder name - will receive real name from creator
      tempVillageName = "Pending...";
      Serial.println("[Join] Passphrase entered: " + passphrase);
      
      // Go straight to username - village name will come from creator
      ui.setExistingVillageName(tempVillageName);
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
      // messenger.setEncryption(&encryption);  // LoRa disabled
      // messenger.setVillageInfo(village.getVillageId(), village.getVillageName(), village.getUsername());  // LoRa disabled
      
      // Configure MQTT (set even if not connected yet)
      mqttMessenger.setEncryption(&encryption);
      mqttMessenger.setVillageInfo(village.getVillageId(), village.getVillageName(), village.getUsername());
      
      if (isCreatingVillage) {
        // Show passphrase for creators
        String infoMsg = "The secret passphrase for\nthis village is:\n\n";
        infoMsg += tempVillagePassword + "\n\n";
        infoMsg += "Only friends you tell it to\ncan join.";
        
        ui.showMessage("Village Created!", infoMsg, 0);
      } else {
        // For joiners, show that they're in
        String infoMsg = "Welcome to " + village.getVillageName() + "!\n\n";
        infoMsg += "You can now chat with\nother members.\n\n";
        infoMsg += "Press ENTER to continue";
        
        ui.showMessage("Village Joined!", infoMsg, 0);
      }
      
      // Wait for enter key to continue
      while (!keyboard.isEnterPressed() && !keyboard.isRightPressed()) {
        keyboard.update();
        smartDelay(50);
      }
      
      // Properly initialize messaging screen (same as menu path)
      Serial.println("[App] Entering messaging. Messages in history: " + String(ui.getMessageCount()));
      keyboard.clearInput();  // Clear buffer to prevent typing detection freeze
      appState = APP_MESSAGING;
      inMessagingScreen = true;  // Set flag - we're now viewing messages
      lastMessagingActivity = millis();  // Record activity time
      ui.setState(STATE_MESSAGING);
      ui.resetMessageScroll();  // Reset scroll to show latest messages
      
      // If we're the owner, send village name announcement for joiners
      if (village.amOwner()) {
        messenger.sendVillageNameAnnouncement();
        Serial.println("[Village] Sent village name announcement");
      }
      // If we're a joiner with "Pending..." name, request the real name
      else if (village.getVillageName() == "Pending...") {
        messenger.sendVillageNameRequest();
        Serial.println("[Village] Requested village name from owner");
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
      
      // DON'T queue read receipts for old messages - they were already read in previous session
      // Read receipts only get sent when NEW messages arrive while in messaging screen
      readReceiptQueue.clear();  // Clear any old queue items
      Serial.println("[App] Not queuing read receipts for historical messages");
      
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
      
      // Send the message via MQTT only (LoRa disabled)
      // messenger.sendMessage(messageText);  // LoRa disabled
      
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
      localMsg.timestamp = max(millis(), timestampBaseline + 1);
      timestampBaseline = localMsg.timestamp;  // Update baseline
      localMsg.received = false;
      localMsg.status = MSG_SENT;
      localMsg.messageId = sentMessageId;  // Use the actual ID from LoRa
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
      
      // Send via MQTT only (LoRa disabled for speed)
      // messenger.sendShout(currentText);  // LoRa disabled
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
      ui.addMessage(sentMsg);
      
      // Save message to disk
      if (!messageId.isEmpty()) {
        village.saveMessage(sentMsg);
      }
      
      // Clear input and switch to messaging view
      ui.setInputText("");
      appState = APP_MESSAGING;
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
  
  // Left arrow to go back
  if (keyboard.isLeftPressed()) {
    keyboard.clearInput();
    appState = APP_MAIN_MENU;
    ui.setState(STATE_VILLAGE_SELECT);
    ui.resetMenuSelection();
    ui.update();
    smartDelay(300);
    return;
  }
  
  if (keyboard.isEnterPressed() || keyboard.isRightPressed()) {
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
