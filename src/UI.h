#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <SPI.h>
#include <vector>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_290_BS.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "Messages.h"
#include "Village.h"

#define SCREEN_WIDTH 296
#define SCREEN_HEIGHT 128

enum UIState {
    STATE_SPLASH,
    STATE_VILLAGE_SELECT,
    STATE_MAIN_MENU,
    STATE_WIFI_SETUP_MENU,
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
    STATE_INPUT_MESSAGE
};

class UI {
private:
    SPIClass* displaySPI;  // Separate SPI bus for display
    GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>* display;
    UIState currentState;
    
    int menuSelection;
    String inputText;
    bool inputComplete;
    
    std::vector<Message> messageHistory;
    int messageScrollOffset;
    
    std::vector<String> memberList;  // Store member list for display
    String existingVillageName;  // Store village name if one exists
    String buildNumber;  // Build version to display
    float batteryVoltage;  // Current battery voltage
    int batteryPercent;    // Current battery percentage
    
    // Callback to check if user is typing (defers display updates during typing)
    bool (*typingCheckCallback)();
    
    void drawSplash();
    void drawVillageSelect();
    void drawMainMenu();
    void drawWiFiSetupMenu();
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
    
public:
    UI();
    
    bool begin(int8_t sck, int8_t miso, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);
    void update();
    void updatePartial();  // Partial refresh for smooth menu navigation
    
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
    
    // Build version
    void setBuildNumber(const String& build) { buildNumber = build; }
    
    // Battery display
    void setBatteryStatus(float voltage, int percent);
    void drawBatteryIcon(int x, int y, int percent);
    
    // Display helpers
    void showMessage(const String& title, const String& message, int durationMs = 2000);
    void clear();
};

#endif
