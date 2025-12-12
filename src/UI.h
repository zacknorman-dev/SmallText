#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_290_BS.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "Messages.h"
#include "Village.h"

#define SCREEN_WIDTH 296
#define SCREEN_HEIGHT 128

enum UIState {
    STATE_SPLASH,
    STATE_VILLAGE_SELECT,
    STATE_MAIN_MENU,
    STATE_SETTINGS_MENU,
    STATE_RINGTONE_SELECT,
    STATE_WIFI_SETUP_MENU,
    STATE_WIFI_NETWORK_LIST,      // New: show scanned networks
    STATE_WIFI_NETWORK_OPTIONS,   // New: connect/forget menu
    STATE_WIFI_NETWORK_DETAILS,   // New: show details of connected network
    STATE_WIFI_SSID_INPUT,
    STATE_WIFI_PASSWORD_INPUT,
    STATE_WIFI_STATUS,
    STATE_OTA_CHECK,
    STATE_OTA_UPDATE,
    STATE_CREATE_VILLAGE,
    STATE_JOIN_VILLAGE_NAME,
    STATE_JOIN_VILLAGE_PASSWORD,
    STATE_INPUT_PASSWORD,
    STATE_JOIN_VILLAGE,
    STATE_VILLAGE_MENU,
    STATE_ADD_MEMBER,
    STATE_VIEW_MEMBERS,
    STATE_MESSAGING,
    STATE_INPUT_TEXT,
    STATE_INPUT_USERNAME,
    STATE_INPUT_MESSAGE,
    STATE_POWERING_DOWN,
    STATE_SLEEPING
};

class UI {
private:
    SPIClass* displaySPI;  // Separate SPI bus for display
    GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>* display;
    UIState currentState;
    // Refresh policy counters
    unsigned long lastFullRefreshMs = 0;
    int partialRefreshCount = 0;
    // Tunables
    static const int MAX_PARTIAL_BEFORE_FULL = 12;   // force full after N partials
    static const unsigned long MAX_PARTIAL_AGE_MS = 15000; // or after 15s
    
    int menuSelection;
    String inputText;
    bool inputComplete;
    
    std::vector<Message> messageHistory;
    int messageScrollOffset;
    
    std::vector<String> memberList;  // Store member list for display
    String existingVillageName;  // Store village name if one exists
    String currentUsername;  // Current user's username for message display
    String buildNumber;  // Build version to display
    float batteryVoltage;  // Current battery voltage
    int batteryPercent;    // Current battery percentage
    bool ringtoneEnabled;  // Ringtone on/off setting
    String ringtoneName;   // Current ringtone name
    
    // WiFi network list storage
    std::vector<String> networkSSIDs;
    std::vector<int> networkRSSIs;
    std::vector<bool> networkEncrypted;
    std::vector<bool> networkSaved;
    
    // WiFi connection status
    String connectedSSID;
    bool isWiFiConnected;
    
    // Callback to check if user is typing (defers display updates during typing)
    bool (*typingCheckCallback)();
    
    void drawSplash();
    void drawVillageSelect();
    void drawMainMenu();
    void drawSettingsMenu();
    void drawRingtoneSelect();
    void drawWiFiSetupMenu();
    void drawWiFiNetworkList();
    void drawWiFiNetworkOptions();
    void drawWiFiNetworkDetails();
    void drawWiFiSSIDInput();
    void drawWiFiPasswordInput();
    void drawWiFiStatus();
    void drawOTACheck();
    void drawOTAUpdate();
    void drawCreateVillage();
    void drawJoinVillage();
    void drawVillageMenu();
    void drawAddMember();
    void drawViewMembers();
    void drawMessaging();
    void drawInputPrompt(const String& prompt);
    void drawPoweringDown();
    void drawSleeping();
    
public:
    UI();
    
    bool begin(int8_t sck, int8_t miso, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);
    void update();
    void updatePartial();  // Partial refresh for smooth menu navigation
    void updateFull();     // Full-screen refresh (multi-phase waveform)
    void updateClean();    // Clear then draw - cleaner transitions than partial alone
    
    // Callback to check if user is typing (for deferring display updates)
    void setTypingCheckCallback(bool (*callback)());
    
    // State management
    void setState(UIState state);
    UIState getState() { return currentState; }
    
    // Menu navigation
    void menuUp();
    void menuDown();
    int getMenuSelection();
    void resetMenuSelection();
    
    // Input handling
    void addInputChar(char c);
    void removeInputChar();
    void setInputText(const String& text);
    String getInputText();
    void clearInputText();
    bool isInputComplete();
    void setInputComplete(bool complete);
    
    // Messaging
    void addMessage(const Message& msg);
    void clearMessages();
    void scrollMessagesUp();
    void scrollMessagesDown();
    void resetMessageScroll();
    int getMessageCount() const;
    void updateMessageStatus(const String& messageId, MessageStatus newStatus);
    
    // Member list
    void setMemberList(const std::vector<String>& members);
    void setExistingVillageName(const String& name);
    void setCurrentUsername(const String& username) { currentUsername = username; }
    
    // Build version
    void setBuildNumber(const String& build) { buildNumber = build; }
    
    // Battery display
    void setBatteryStatus(float voltage, int percent);
    void drawBatteryIcon(int x, int y, int percent);
    
    // Ringtone setting
    void setRingtoneEnabled(bool enabled) { ringtoneEnabled = enabled; }
    bool getRingtoneEnabled() const { return ringtoneEnabled; }
    void setRingtoneName(const String& name) { ringtoneName = name; }
    String getRingtoneName() const { return ringtoneName; }
    
    // WiFi network list
    void setNetworkList(const std::vector<String>& ssids, const std::vector<int>& rssis, 
                        const std::vector<bool>& encrypted, const std::vector<bool>& saved);
    int getNetworkCount() const { return networkSSIDs.size(); }
    String getNetworkSSID(int index) const { return (index >= 0 && index < networkSSIDs.size()) ? networkSSIDs[index] : ""; }
    
    // WiFi connection status
    void setWiFiConnected(bool connected, const String& ssid = "") { isWiFiConnected = connected; connectedSSID = ssid; }
    bool getWiFiConnected() const { return isWiFiConnected; }
    String getConnectedSSID() const { return connectedSSID; }
    
    // Display helpers
    void showMessage(const String& title, const String& message, int durationMs = 2000);
    void showPoweringDown();
    void showSleepScreen();
    void showNappingScreen(float batteryVoltage, bool hasWiFi = true);
    void showLowBatteryScreen(float batteryVoltage);
    void clear();
};

#endif
