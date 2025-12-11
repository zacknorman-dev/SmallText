#include "UI.h"

UI::UI() {
    displaySPI = nullptr;
    display = nullptr;
    currentState = STATE_SPLASH;
    menuSelection = 0;
    inputText = "";
    inputComplete = false;
    messageScrollOffset = 0;
    typingCheckCallback = nullptr;
    batteryVoltage = 0.0;
    batteryPercent = 0;
    ringtoneEnabled = true;  // Default to on
    ringtoneName = "Rising Tone";  // Default ringtone name
}

bool UI::begin(int8_t sck, int8_t miso, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy) {
    // Create separate SPI bus for display (HSPI)
    displaySPI = new SPIClass(HSPI);
    displaySPI->begin(sck, miso, mosi, cs);
    
    // Create GxEPD2 display instance
    display = new GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>(GxEPD2_290_BS(cs, dc, rst, busy));
    
    // Tell the display to use our custom SPI bus
    display->epd2.selectSPI(*displaySPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    
    // Initialize display WITHOUT initial refresh to avoid blocking
    display->init(115200, false, 2, false);
    display->setRotation(1);  // 90 degrees counterclockwise for landscape
    display->setTextColor(GxEPD_BLACK);
    display->setFullWindow();  // Ensure full window is set
    
    Serial.println(F("[UI] GxEPD2 display initialized"));
    return true;
}

void UI::update() {
    // Note: Don't defer the first update after state changes, always let it draw
    // Only defer subsequent updates during active typing to keep keyboard responsive
    if (typingCheckCallback && typingCheckCallback() && currentState == STATE_MESSAGING) {
        return;  // Skip display refresh during typing in messaging screen only
    }
    
    // Use partial refresh for fast, no-flash updates
    display->setPartialWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    display->fillScreen(GxEPD_WHITE);
    
    switch (currentState) {
        case STATE_SPLASH:
            drawSplash();
            break;
        case STATE_VILLAGE_SELECT:
            drawVillageSelect();
            break;
        case STATE_MAIN_MENU:
            drawMainMenu();
            break;
        case STATE_WIFI_SETUP_MENU:
            drawWiFiSetupMenu();
            break;
        case STATE_WIFI_SSID_INPUT:
            drawWiFiSSIDInput();
            break;
        case STATE_WIFI_PASSWORD_INPUT:
            drawWiFiPasswordInput();
            break;
        case STATE_WIFI_STATUS:
            drawWiFiStatus();
            break;
        case STATE_OTA_CHECK:
            drawOTACheck();
            break;
        case STATE_OTA_UPDATE:
            drawOTAUpdate();
            break;
        case STATE_VILLAGE_MENU:
            drawVillageMenu();
            break;
        case STATE_CREATE_VILLAGE:
            drawInputPrompt("Village name:");
            break;
        case STATE_JOIN_VILLAGE_NAME:
            drawInputPrompt("Village to join:");
            break;
        case STATE_JOIN_VILLAGE_PASSWORD:
            drawInputPrompt("Enter secret passphrase:");
            break;
        case STATE_INPUT_PASSWORD:
            drawInputPrompt("Village password:");
            break;
        case STATE_ADD_MEMBER:
            drawInputPrompt("Member ID:");
            break;
        case STATE_VIEW_MEMBERS:
            drawViewMembers();
            break;
        case STATE_MESSAGING:
            drawMessaging();
            break;
        case STATE_INPUT_TEXT:
            drawInputPrompt("Enter text:");
            break;
        case STATE_INPUT_USERNAME:
            drawInputPrompt("Your display name:");
            break;
        case STATE_INPUT_MESSAGE:
            drawInputPrompt("New message:");
            break;
        case STATE_POWERING_DOWN:
            drawPoweringDown();
            break;
        case STATE_SLEEPING:
            drawSleeping();
            break;
    }
    
    display->display(true);  // Partial refresh - no flash
}

void UI::updatePartial() {
    // Fast partial refresh - single draw, no loop
    display->setPartialWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    display->fillScreen(GxEPD_WHITE);
    
    switch (currentState) {
        case STATE_VILLAGE_SELECT:
            drawVillageSelect();
            break;
        case STATE_MAIN_MENU:
            drawMainMenu();
            break;
        case STATE_SETTINGS_MENU:
            drawSettingsMenu();
            break;
        case STATE_RINGTONE_SELECT:
            drawRingtoneSelect();
            break;
        case STATE_WIFI_SETUP_MENU:
            drawWiFiSetupMenu();
            break;
        case STATE_WIFI_SSID_INPUT:
            drawWiFiSSIDInput();
            break;
        case STATE_WIFI_PASSWORD_INPUT:
            drawWiFiPasswordInput();
            break;
        case STATE_WIFI_STATUS:
            drawWiFiStatus();
            break;
        case STATE_OTA_CHECK:
            drawOTACheck();
            break;
        case STATE_OTA_UPDATE:
            drawOTAUpdate();
            break;
        case STATE_VILLAGE_MENU:
            drawVillageMenu();
            break;
        case STATE_CREATE_VILLAGE:
            drawInputPrompt("Village name:");
            break;
        case STATE_JOIN_VILLAGE_NAME:
            drawInputPrompt("Village to join:");
            break;
        case STATE_JOIN_VILLAGE_PASSWORD:
            drawInputPrompt("Enter secret passphrase:");
            break;
        case STATE_INPUT_PASSWORD:
            drawInputPrompt("Village password:");
            break;
        case STATE_INPUT_USERNAME:
            drawInputPrompt("Your display name:");
            break;
        case STATE_INPUT_MESSAGE:
            drawInputPrompt("New message:");
            break;
        case STATE_MESSAGING:
            drawMessaging();
            break;
        default:
            // For other states, do full refresh
            break;
    }
    
    display->display(true);  // Partial refresh
}

void UI::updateClean() {
    // Clean transition: clear to white with partial, then draw content with partial
    // This minimizes ghosting better than single partial refresh
    display->setPartialWindow(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    display->fillScreen(GxEPD_WHITE);
    display->display(true);  // Partial refresh to clear
    
    // Now draw the actual content
    display->fillScreen(GxEPD_WHITE);
    switch (currentState) {
        case STATE_SPLASH:          drawSplash(); break;
        case STATE_VILLAGE_SELECT:  drawVillageSelect(); break;
        case STATE_MAIN_MENU:       drawMainMenu(); break;
        case STATE_SETTINGS_MENU:   drawSettingsMenu(); break;
        case STATE_RINGTONE_SELECT: drawRingtoneSelect(); break;
        case STATE_WIFI_SETUP_MENU: drawWiFiSetupMenu(); break;
        case STATE_WIFI_SSID_INPUT: drawWiFiSSIDInput(); break;
        case STATE_WIFI_PASSWORD_INPUT: drawWiFiPasswordInput(); break;
        case STATE_WIFI_STATUS:     drawWiFiStatus(); break;
        case STATE_OTA_CHECK:       drawOTACheck(); break;
        case STATE_OTA_UPDATE:      drawOTAUpdate(); break;
        case STATE_CREATE_VILLAGE:  drawInputPrompt("Village name:"); break;
        case STATE_JOIN_VILLAGE_NAME: drawInputPrompt("Village to join:"); break;
        case STATE_JOIN_VILLAGE_PASSWORD: drawInputPrompt("Enter secret passphrase:"); break;
        case STATE_INPUT_PASSWORD:  drawInputPrompt("Village password:"); break;
        case STATE_ADD_MEMBER:      drawAddMember(); break;
        case STATE_VIEW_MEMBERS:    drawViewMembers(); break;
        case STATE_MESSAGING:       drawMessaging(); break;
        case STATE_INPUT_TEXT:      drawInputPrompt("Enter text:"); break;
        case STATE_INPUT_USERNAME:  drawInputPrompt("Your display name:"); break;
        case STATE_INPUT_MESSAGE:   drawInputPrompt("New message:"); break;
        case STATE_POWERING_DOWN:   drawPoweringDown(); break;
        case STATE_SLEEPING:        drawSleeping(); break;
        case STATE_VILLAGE_MENU:    drawVillageMenu(); break;
    }
    display->display(true);  // Partial refresh to draw content
}

void UI::updateFull() {
    // Full refresh: set full window, draw current state, then use full waveform
    display->setFullWindow();
    display->fillScreen(GxEPD_WHITE);
    switch (currentState) {
        case STATE_SPLASH:          drawSplash(); break;
        case STATE_VILLAGE_SELECT:  drawVillageSelect(); break;
        case STATE_MAIN_MENU:       drawMainMenu(); break;
        case STATE_SETTINGS_MENU:   drawSettingsMenu(); break;
        case STATE_RINGTONE_SELECT: drawRingtoneSelect(); break;
        case STATE_WIFI_SETUP_MENU: drawWiFiSetupMenu(); break;
        case STATE_WIFI_SSID_INPUT: drawWiFiSSIDInput(); break;
        case STATE_WIFI_PASSWORD_INPUT: drawWiFiPasswordInput(); break;
        case STATE_WIFI_STATUS:     drawWiFiStatus(); break;
        case STATE_OTA_CHECK:       drawOTACheck(); break;
        case STATE_OTA_UPDATE:      drawOTAUpdate(); break;
        case STATE_CREATE_VILLAGE:  drawInputPrompt("Village name:"); break;
        case STATE_JOIN_VILLAGE_NAME: drawInputPrompt("Village to join:"); break;
        case STATE_JOIN_VILLAGE_PASSWORD: drawInputPrompt("Enter secret passphrase:"); break;
        case STATE_INPUT_PASSWORD:  drawInputPrompt("Village password:"); break;
        case STATE_ADD_MEMBER:      drawAddMember(); break;
        case STATE_VIEW_MEMBERS:    drawViewMembers(); break;
        case STATE_MESSAGING:       drawMessaging(); break;
        case STATE_INPUT_TEXT:      drawInputPrompt("Enter text:"); break;
        case STATE_INPUT_USERNAME:  drawInputPrompt("Your display name:"); break;
        case STATE_INPUT_MESSAGE:   drawInputPrompt("New message:"); break;
        case STATE_POWERING_DOWN:   drawPoweringDown(); break;
        case STATE_SLEEPING:        drawSleeping(); break;
    }
    display->display(false);  // Full refresh (multi-phase, clears ghosting)
}

void UI::setTypingCheckCallback(bool (*callback)()) {
    typingCheckCallback = callback;
}

void UI::setState(UIState state) {
    currentState = state;
    menuSelection = 0;
    // Don't call update() here - let caller control when to refresh display
    // This prevents blocking during critical initialization sequences
}

void UI::drawSplash() {
    // Large smolTxt title (note lowercase 's')
    display->setFont(&FreeSansBold24pt7b);
    display->setCursor(55, 55);
    display->print("smolTxt");
    
    // Single-line subtitle (moved right 22px)
    display->setFont(&FreeSans9pt7b);
    display->setCursor(72, 85);
    display->print("Safe text for kids");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
}

void UI::drawVillageSelect() {
    // Title - bold 9pt
    display->setFont(&FreeSansBold9pt7b);
    display->setCursor(10, 18);
    display->print("Select Conversation");
    
    // Horizontal line under title
    display->drawLine(0, 22, SCREEN_WIDTH, 22, GxEPD_BLACK);
    
    // Battery icon at original position
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 35;
    int lineHeight = 18;
    int item = 0;
    int scrollOffset = 0;
    
    // Count total items to determine scroll offset
    int totalItems = 0;
    for (int slot = 0; slot < 10; slot++) {
        if (Village::getVillageNameFromSlot(slot).length() > 0) {
            totalItems++;
        }
    }
    totalItems += 3; // + New, Join, WiFi
    
    // Calculate scroll offset if selection is beyond visible area
    const int maxVisibleItems = 5;
    if (menuSelection >= maxVisibleItems) {
        scrollOffset = menuSelection - maxVisibleItems + 1;
    }
    
    // List all saved villages from slots 0-9
    for (int slot = 0; slot < 10; slot++) {
        String villageName = Village::getVillageNameFromSlot(slot);
        if (villageName.length() > 0) {
            // Skip items that are scrolled off the top
            if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
                if (menuSelection == item) {
                    display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
                    display->setTextColor(GxEPD_WHITE);
                }
                display->setCursor(10, y);
                display->print(villageName);
                if (menuSelection == item) {
                    display->setTextColor(GxEPD_BLACK);
                }
                y += lineHeight;
            }
            item++;
        }
    }
    
    // New Conversation
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("New Conversation");
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
        y += lineHeight;
    }
    item++;
    
    // Join Conversation
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("Join Conversation");
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
        y += lineHeight;
    }
    item++;
    
    // Settings
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("Settings");
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
    }
    item++;
    
    // Draw down-arrow if there are more items below the visible area
    int lastVisibleItem = scrollOffset + maxVisibleItems - 1;
    if (totalItems > maxVisibleItems && lastVisibleItem < totalItems - 1) {
        // Draw small equilateral triangle pointing down (10px wide, 10px high)
        int arrowX = 10;
        int arrowY = SCREEN_HEIGHT - 15;
        int arrowWidth = 10;
        int arrowHeight = 10;
        
        // Draw filled triangle: three points forming downward arrow
        display->fillTriangle(
            arrowX, arrowY,                           // Top left
            arrowX + arrowWidth, arrowY,              // Top right
            arrowX + arrowWidth/2, arrowY + arrowHeight, // Bottom center
            GxEPD_BLACK
        );
    }
}

void UI::drawMainMenu() {
    // Title
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(80, 20);
    display->print("MAIN MENU");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 50;
    int lineHeight = 25;
    
    // Create Village
    if (menuSelection == 0) {
        display->fillRect(5, y - 18, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Create Village");
    if (menuSelection == 0) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    
    // Join Village
    if (menuSelection == 1) {
        display->fillRect(5, y - 18, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Join Village");
    if (menuSelection == 1) {
        display->setTextColor(GxEPD_BLACK);
    }
}

void UI::drawVillageMenu() {
    // Title - show conversation name in bold 9pt
    display->setFont(&FreeSansBold9pt7b);
    display->setCursor(10, 18);
    display->print(existingVillageName);  // Show conversation name instead of "VILLAGE MENU"
    
    // Horizontal line under title
    display->drawLine(0, 22, SCREEN_WIDTH, 22, GxEPD_BLACK);
    
    // Battery icon at original position
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 38;
    int lineHeight = 20;
    
    // Messages
    if (menuSelection == 0) {
        display->fillRect(5, y - 15, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Messages");
    if (menuSelection == 0) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    
    // Add Member
    if (menuSelection == 1) {
        display->fillRect(5, y - 15, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Add Member");
    if (menuSelection == 1) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    
    // View Members
    if (menuSelection == 2) {
        display->fillRect(5, y - 15, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("View Members");
    if (menuSelection == 2) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    
    // Delete Group
    if (menuSelection == 3) {
        display->fillRect(5, y - 15, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Delete Group");
    if (menuSelection == 3) {
        display->setTextColor(GxEPD_BLACK);
    }
}

void UI::drawSettingsMenu() {
    // Title - bold 9pt
    display->setFont(&FreeSansBold9pt7b);
    display->setCursor(10, 18);
    display->print("Settings");
    
    // Horizontal line under title
    display->drawLine(0, 22, SCREEN_WIDTH, 22, GxEPD_BLACK);
    
    // Battery icon
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 35;
    int lineHeight = 18;
    int item = 0;
    int scrollOffset = 0;
    
    const int totalItems = 3;
    const int maxVisibleItems = 5;
    
    // Calculate scroll offset
    if (menuSelection >= maxVisibleItems) {
        scrollOffset = menuSelection - maxVisibleItems + 1;
    }
    
    // Ringtone
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("Ringtone: " + ringtoneName);
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
        y += lineHeight;
    }
    item++;
    
    // WiFi
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("WiFi");
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
        y += lineHeight;
    }
    item++;
    
    // Updates
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("Updates");
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
    }
}

void UI::drawRingtoneSelect() {
    // Title - bold 9pt
    display->setFont(&FreeSansBold9pt7b);
    display->setCursor(10, 18);
    display->print("Select Ringtone");
    
    // Horizontal line under title
    display->drawLine(0, 22, SCREEN_WIDTH, 22, GxEPD_BLACK);
    
    // Battery icon
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 35;
    int lineHeight = 18;
    int scrollOffset = 0;
    
    const int totalItems = 12;
    const int maxVisibleItems = 5;
    
    // Calculate scroll offset - keep selection visible
    if (menuSelection >= maxVisibleItems) {
        scrollOffset = menuSelection - maxVisibleItems + 1;
    }
    
    // Get ringtone names from main.cpp
    extern const char* ringtoneNames[];
    
    // Draw visible ringtone options
    for (int i = 0; i < totalItems; i++) {
        if (i >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
            if (menuSelection == i) {
                display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
                display->setTextColor(GxEPD_WHITE);
            }
            display->setCursor(10, y);
            display->print(ringtoneNames[i]);
            if (menuSelection == i) {
                display->setTextColor(GxEPD_BLACK);
            }
            y += lineHeight;
        }
    }
    
    // Draw down-arrow if there are more items below
    int lastVisibleItem = scrollOffset + maxVisibleItems - 1;
    if (totalItems > maxVisibleItems && lastVisibleItem < totalItems - 1) {
        int arrowX = 10;
        int arrowY = SCREEN_HEIGHT - 15;
        int arrowWidth = 10;
        int arrowHeight = 10;
        
        display->fillTriangle(
            arrowX, arrowY,
            arrowX + arrowWidth, arrowY,
            arrowX + arrowWidth/2, arrowY + arrowHeight,
            GxEPD_BLACK
        );
    }
}

void UI::drawWiFiSetupMenu() {
    // Title - bold 9pt
    display->setFont(&FreeSansBold9pt7b);
    display->setCursor(10, 18);
    display->print("WiFi");
    
    // Horizontal line under title
    display->drawLine(0, 22, SCREEN_WIDTH, 22, GxEPD_BLACK);
    
    // Battery icon
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 35;
    int lineHeight = 18;
    int item = 0;
    int scrollOffset = 0;
    
    const int totalItems = 2;
    const int maxVisibleItems = 5;
    
    // Calculate scroll offset
    if (menuSelection >= maxVisibleItems) {
        scrollOffset = menuSelection - maxVisibleItems + 1;
    }
    
    // Configure WiFi
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("Configure WiFi");
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
        y += lineHeight;
    }
    item++;
    
    // Check Connection
    if (item >= scrollOffset && y <= SCREEN_HEIGHT - 5) {
        if (menuSelection == item) {
            display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
            display->setTextColor(GxEPD_WHITE);
        }
        display->setCursor(10, y);
        display->print("Check Connection");
        if (menuSelection == item) {
            display->setTextColor(GxEPD_BLACK);
        }
    }
}

void UI::drawWiFiSSIDInput() {
    drawInputPrompt("WiFi Network (SSID):");
}

void UI::drawWiFiPasswordInput() {
    drawInputPrompt("WiFi Password:");
}

void UI::drawWiFiStatus() {
    // Title
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(60, 20);
    display->print("WiFi Status");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 50;
    int lineHeight = 20;
    
    // Show connection info from inputText (main.cpp will format this)
    // Expected format: "Connected\n192.168.1.100\n-65 dBm"
    display->setCursor(10, y);
    
    // Simple multi-line display
    int lineStart = 0;
    String statusText = inputText;
    for (int i = 0; i <= statusText.length(); i++) {
        if (i == statusText.length() || statusText[i] == '\n') {
            String line = statusText.substring(lineStart, i);
            display->setCursor(10, y);
            display->print(line);
            y += lineHeight;
            lineStart = i + 1;
        }
    }
    
    // Footer hint
    display->setFont();
    display->setCursor(5, SCREEN_HEIGHT - 8);
    display->print("ENTER:continue");
}

void UI::drawOTACheck() {
    // Title
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(30, 20);
    display->print("Check for Updates");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 50;
    int lineHeight = 20;
    
    // Show update status from inputText
    // Expected format: "Checking...\nCurrent: v0.13.0"
    display->setCursor(10, y);
    
    int lineStart = 0;
    String statusText = inputText;
    for (int i = 0; i <= statusText.length(); i++) {
        if (i == statusText.length() || statusText[i] == '\n') {
            String line = statusText.substring(lineStart, i);
            display->setCursor(10, y);
            display->print(line);
            y += lineHeight;
            lineStart = i + 1;
        }
    }
    
    // Footer hint
    display->setFont();
    display->setCursor(5, SCREEN_HEIGHT - 8);
    display->print("ENTER:continue/update <-:cancel");
}

void UI::drawOTAUpdate() {
    // Title
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(60, 20);
    display->print("Updating...");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 60;
    
    // Show progress from inputText
    // Expected format: "Downloading...\n45%"
    display->setCursor(10, y);
    
    int lineStart = 0;
    String statusText = inputText;
    for (int i = 0; i <= statusText.length(); i++) {
        if (i == statusText.length() || statusText[i] == '\n') {
            String line = statusText.substring(lineStart, i);
            display->setCursor(10, y);
            display->print(line);
            y += 25;
            lineStart = i + 1;
        }
    }
    
    // Footer
    display->setFont();
    display->setCursor(5, SCREEN_HEIGHT - 8);
    display->print("Please wait...");
}

void UI::drawCreateVillage() {
    drawInputPrompt("Village name:");
}

void UI::drawJoinVillage() {
    drawInputPrompt("Village ID:");
}

void UI::drawAddMember() {
    drawInputPrompt("Member ID:");
}

void UI::drawViewMembers() {
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(70, 20);
    display->print("MEMBERS");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 45;
    int lineHeight = 15;
    int maxMembers = 6;
    
    for (int i = 0; i < memberList.size() && i < maxMembers; i++) {
        display->setCursor(10, y);
        display->print(memberList[i]);
        y += lineHeight;
    }
    
    if (memberList.size() == 0) {
        display->setCursor(10, y);
        display->print("No members yet");
    }
}

void UI::drawMessaging() {
    Serial.println("[UI] Drawing messaging. History size: " + String(messageHistory.size()));
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int lineHeight = 16;
    int leftMargin = 5;
    int rightMargin = 5;
    int maxLineWidth = SCREEN_WIDTH - leftMargin - rightMargin;  // Available width in pixels
    
    // Cursor gets a full line at the bottom
    int cursorY = SCREEN_HEIGHT - 4;  // Full line height baseline
    
    // Messages fill screen from top to just above cursor line
    int topY = 15;  // Allow partial text at top edge
    int bottomY = cursorY - lineHeight;  // One full line above cursor
    
    if (messageHistory.size() == 0) {
        display->setCursor(10, 60);
        display->print("No messages yet");
    } else {
        int msgCount = (int)messageHistory.size();
        
        // Build wrapped lines for ALL messages, newest to oldest
        // Each message becomes multiple display lines
        // messageScrollOffset tells us how many display LINES to scroll up
        
        // First pass: calculate all wrapped lines for all messages
        // Store lines with status info so we can render status in small font
        struct DisplayLine {
            String text;
            String status;  // Empty or "(sent)", "(read)", etc.
            bool isFirstLine;  // First line of message (sender name bold)
            String senderPart;  // "You: " or "Name: " for bold rendering
        };
        std::vector<DisplayLine> allLines;
        
        for (int i = msgCount - 1; i >= 0; i--) {  // Start from most recent
            const Message& msg = messageHistory[i];
            
            // Build the message text WITHOUT status
            String msgText = "";
            if (msg.sender == currentUsername) {
                msgText = "You: ";
            } else {
                String sender = msg.sender;
                if (sender.length() > 8) {
                    sender = sender.substring(0, 8);
                }
                msgText = sender + ": ";
            }
            
            msgText += msg.content;
            
            // Determine status text (if any)
            String statusText = "";
            if (!msg.received) {
                switch (msg.status) {
                    case MSG_SENT:
                        statusText = " (sent)";
                        break;
                    case MSG_RECEIVED:
                        statusText = " (rec'd)";
                        break;
                    case MSG_SEEN:
                        statusText = " (rec'd)";
                        break;
                    case MSG_READ:
                        statusText = " (read)";
                        break;
                }
            }
            
            // Word wrap the message text (without status) using pixel width
            // Collect all lines for THIS message first
            std::vector<DisplayLine> messageLines;
            String remainingText = msgText;
            bool firstLineOfMessage = true;
            
            // Extract sender part for bold rendering (without trailing space)
            String senderPart = "";
            if (msg.sender == currentUsername) {
                senderPart = "You:";
            } else {
                String sender = msg.sender;
                if (sender.length() > 8) {
                    sender = sender.substring(0, 8);
                }
                senderPart = sender + ":";
            }
            
            while (remainingText.length() > 0) {
                DisplayLine dLine;
                dLine.isFirstLine = firstLineOfMessage;
                dLine.senderPart = firstLineOfMessage ? senderPart : "";
                
                int availableWidth = maxLineWidth;
                
                // For first line, account for bold sender width + space
                if (firstLineOfMessage && senderPart.length() > 0) {
                    int16_t x1, y1;
                    uint16_t w, h;
                    display->setFont(&FreeSansBold9pt7b);
                    display->getTextBounds(senderPart, 0, 0, &x1, &y1, &w, &h);
                    int senderWidth = w;
                    
                    // Add space width
                    display->setFont(&FreeSans9pt7b);
                    display->getTextBounds(" ", 0, 0, &x1, &y1, &w, &h);
                    int spaceWidth = w;
                    
                    availableWidth = maxLineWidth - senderWidth - spaceWidth;
                    
                    // For first line, remainingText is "You: message"
                    // We need to extract just "message" part for measurement
                    int colonPos = remainingText.indexOf(':');
                    if (colonPos >= 0 && colonPos + 2 < remainingText.length()) {
                        remainingText = remainingText.substring(colonPos + 2);  // Skip ": "
                    }
                }
                
                firstLineOfMessage = false;
                
                // Measure how much text fits in availableWidth
                int16_t x1, y1;
                uint16_t w, h;
                display->setFont(&FreeSans9pt7b);
                display->getTextBounds(remainingText, 0, 0, &x1, &y1, &w, &h);
                
                if (w <= availableWidth) {
                    // Entire remaining text fits on one line
                    dLine.text = remainingText;
                    dLine.status = statusText;  // Status on last line
                    remainingText = "";
                } else {
                    // Need to wrap - find longest substring that fits
                    int breakPoint = -1;
                    int lastSpacePos = -1;
                    
                    // Binary search for the longest fitting substring
                    int left = 1;
                    int right = remainingText.length();
                    int bestFit = 1;
                    
                    while (left <= right) {
                        int mid = (left + right) / 2;
                        String testStr = remainingText.substring(0, mid);
                        display->getTextBounds(testStr, 0, 0, &x1, &y1, &w, &h);
                        
                        if (w <= availableWidth) {
                            bestFit = mid;
                            left = mid + 1;
                        } else {
                            right = mid - 1;
                        }
                    }
                    
                    // Now find last space before bestFit position
                    for (int j = bestFit; j > 0; j--) {
                        if (remainingText.charAt(j) == ' ') {
                            lastSpacePos = j;
                            break;
                        }
                    }
                    
                    // If we found a space, break there; otherwise force break at bestFit
                    if (lastSpacePos > 0 && lastSpacePos > bestFit / 2) {
                        breakPoint = lastSpacePos;
                    } else {
                        breakPoint = bestFit;
                    }
                    
                    dLine.text = remainingText.substring(0, breakPoint);
                    dLine.status = "";  // No status on wrapped lines
                    
                    // Move past the break point and trim leading space
                    remainingText = remainingText.substring(breakPoint);
                    if (remainingText.startsWith(" ")) {
                        remainingText = remainingText.substring(1);
                    }
                }
                
                messageLines.push_back(dLine);
            }
            
            // Add this message's lines to allLines in REVERSE order
            // (last continuation line first, then first line last)
            // This way when drawn from bottom up, first line appears at bottom
            for (int j = messageLines.size() - 1; j >= 0; j--) {
                allLines.push_back(messageLines[j]);
            }
        }
        
        // Now draw lines with scroll offset applied
        // allLines[0] = most recent message's first line
        // messageScrollOffset = number of MESSAGE POSTS to skip (not lines)
        // When scrollOffset > 0, we skip recent messages and show older ones
        
        int totalLines = allLines.size();
        
        // Calculate how many complete messages to skip
        // We need to count messages, not lines
        // Recount messages and their line counts
        int linesSoFar = 0;
        int linesToSkip = 0;
        
        // Count lines to skip based on messageScrollOffset (whole messages)
        // We already calculated all lines in allLines, just count them by message
        if (messageScrollOffset > 0) {
            int msgsSkipped = 0;
            int lineIdx = 0;
            
            // Walk through allLines counting complete messages
            while (lineIdx < totalLines && msgsSkipped < messageScrollOffset) {
                // Skip all lines of this message
                while (lineIdx < totalLines) {
                    linesToSkip++;
                    lineIdx++;
                    // If we hit a first line (next message), stop
                    if (lineIdx < totalLines && allLines[lineIdx].isFirstLine) {
                        break;
                    }
                }
                msgsSkipped++;
            }
        }
        
        // Draw from bottom to top, starting after skipped lines
        int currentY = bottomY;
        for (int i = linesToSkip; i < totalLines && currentY >= topY - lineHeight; i++) {  // Allow partial at top
            int xPos = leftMargin;
            
            // If first line, render sender name in bold
            if (allLines[i].isFirstLine && allLines[i].senderPart.length() > 0) {
                display->setFont(&FreeSansBold9pt7b);
                display->setCursor(xPos, currentY);
                display->print(allLines[i].senderPart);
                
                // Calculate width of sender part to position message text
                int16_t x1, y1;
                uint16_t w, h;
                display->getTextBounds(allLines[i].senderPart, 0, 0, &x1, &y1, &w, &h);
                xPos += w;
                
                // Render space and message in regular font
                // Note: allLines[i].text now contains ONLY the message content (no sender prefix)
                display->setFont(&FreeSans9pt7b);
                display->setCursor(xPos, currentY);
                display->print(" ");  // Space after colon
                display->print(allLines[i].text);
            } else {
                // Continuation line - just regular font
                display->setFont(&FreeSans9pt7b);
                display->setCursor(xPos, currentY);
                display->print(allLines[i].text);
            }
            
            // Add status in small font if present
            if (allLines[i].status.length() > 0) {
                display->setFont();  // Small default font
                display->print(allLines[i].status);
            }
            
            currentY -= lineHeight;  // Next line goes up
        }
    }
    
    // Cursor at the bottom with full line height
    display->setFont(&FreeSans9pt7b);
    display->setCursor(5, cursorY);
    display->print(">");
    
    // Show current input text with pixel-based scrolling to keep cursor visible
    int promptX = 15;  // X position after ">"
    int availableWidth = SCREEN_WIDTH - promptX - 15;  // Leave room for cursor and margin
    
    String displayText = inputText;
    if (displayText.length() > 0) {
        // Measure the full text width
        int16_t x1, y1;
        uint16_t w, h;
        display->getTextBounds(displayText, 0, 0, &x1, &y1, &w, &h);
        
        if (w > availableWidth) {
            // Text is too long - show the rightmost part that fits
            // Binary search for the longest substring from the END that fits
            int left = 1;
            int right = displayText.length();
            int bestFit = 1;
            
            while (left <= right) {
                int mid = (left + right) / 2;
                String testStr = displayText.substring(displayText.length() - mid);
                display->getTextBounds(testStr, 0, 0, &x1, &y1, &w, &h);
                
                if (w <= availableWidth) {
                    bestFit = mid;
                    left = mid + 1;
                } else {
                    right = mid - 1;
                }
            }
            
            // Show only the last 'bestFit' characters
            displayText = displayText.substring(displayText.length() - bestFit);
        }
    }
    
    display->setCursor(promptX, cursorY);
    display->print(displayText);
    
    // Cursor indicator
    display->print("_");
}

void UI::drawInputPrompt(const String& prompt) {
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(10, 20);
    display->print(prompt);
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    // Draw input area box
    display->drawRect(5, 30, SCREEN_WIDTH - 10, 60, GxEPD_BLACK);
    
    display->setFont(&FreeSans9pt7b);
    
    // Word-wrap the input text
    int x = 10;
    int y = 50;
    int lineHeight = 18;
    int maxWidth = SCREEN_WIDTH - 20;
    String displayText = inputText;
    
    // Simple word wrapping
    int lineStart = 0;
    while (lineStart < displayText.length()) {
        // Find how many chars fit on this line
        int lineEnd = lineStart;
        int lineWidth = 0;
        
        while (lineEnd < displayText.length()) {
            char c = displayText[lineEnd];
            int charWidth = 8;  // Approximate
            if (lineWidth + charWidth > maxWidth) break;
            lineWidth += charWidth;
            lineEnd++;
        }
        
        // Display this line
        display->setCursor(x, y);
        display->print(displayText.substring(lineStart, lineEnd));
        
        lineStart = lineEnd;
        y += lineHeight;
        
        if (y > 85) break;  // Stop if we run out of room
    }
    
    // Show cursor at end
    display->print("_");
    
    // Show character count at bottom
    display->setFont();
    display->setCursor(10, SCREEN_HEIGHT - 10);
    display->print("Chars: ");
    display->print(inputText.length());
    display->print("/130");
    
    // Footer hint
    display->setCursor(SCREEN_WIDTH - 130, SCREEN_HEIGHT - 10);
    display->print("<-:cancel ENTER:send");
}

// Navigation
void UI::menuUp() {
    if (menuSelection > 0) {
        menuSelection--;
    } else {
        // Already at top, don't trigger redraw
        return;
    }
}

void UI::menuDown() {
    int maxItems = 0;
    switch (currentState) {
        case STATE_VILLAGE_SELECT: {
            // Count saved villages + "New Village" + "Join Village" + "WiFi & Updates"
            int villageCount = 0;
            for (int i = 0; i < 10; i++) {
                if (Village::hasVillageInSlot(i)) {
                    villageCount++;
                }
            }
            maxItems = villageCount + 2;  // +2 for "New Village", "Join Village", and "WiFi & Updates" (last index)
            break;
        }
        case STATE_MAIN_MENU:
            maxItems = 1;
            break;
        case STATE_SETTINGS_MENU:
            maxItems = 2;  // Ringtone, WiFi, Updates (0-2)
            break;
        case STATE_RINGTONE_SELECT:
            maxItems = 11;  // 12 ringtone options (0-11)
            break;
        case STATE_VILLAGE_MENU:
            maxItems = 3;
            break;
        case STATE_WIFI_SETUP_MENU:
            maxItems = 1;  // Configure WiFi, Check Connection (0-1)
            break;
        default:
            maxItems = 0;
    }
    
    if (menuSelection < maxItems) {
        menuSelection++;
    } else {
        // Already at bottom, don't trigger redraw
        return;
    }
}

int UI::getMenuSelection() {
    return menuSelection;
}

void UI::resetMenuSelection() {
    menuSelection = 0;
}

// Input handling
void UI::addInputChar(char c) {
    inputText += c;
}

void UI::removeInputChar() {
    if (inputText.length() > 0) {
        inputText.remove(inputText.length() - 1);
    }
}

void UI::setInputText(const String& text) {
    inputText = text;
}

String UI::getInputText() {
    return inputText;
}

void UI::clearInputText() {
    inputText = "";
}

bool UI::isInputComplete() {
    return inputComplete;
}

void UI::setInputComplete(bool complete) {
    inputComplete = complete;
}

// Messaging
void UI::addMessage(const Message& msg) {
    messageHistory.push_back(msg);
    messageScrollOffset = 0;  // Reset scroll to show new message at bottom
    Serial.println("[UI] Message added. Total: " + String(messageHistory.size()));
}

void UI::clearMessages() {
    messageHistory.clear();
}

void UI::scrollMessagesUp() {
    // UP = go back in time = show older messages
    messageScrollOffset++;
}

void UI::scrollMessagesDown() {
    // DOWN = go forward in time = show newer messages
    if (messageScrollOffset > 0) {
        messageScrollOffset--;
    }
}

void UI::resetMessageScroll() {
    messageScrollOffset = 0;
}

int UI::getMessageCount() const {
    return messageHistory.size();
}

void UI::updateMessageStatus(const String& messageId, MessageStatus newStatus) {
    for (Message& msg : messageHistory) {
        if (msg.messageId == messageId) {
            msg.status = newStatus;
            Serial.println("[UI] Updated message " + messageId + " to status " + String((int)newStatus));
            return;
        }
    }
    Serial.println("[UI] Message not found: " + messageId);
}

// Member list
void UI::setMemberList(const std::vector<String>& members) {
    memberList = members;
}

void UI::setExistingVillageName(const String& name) {
    existingVillageName = name;
}

// Display helpers
void UI::showMessage(const String& title, const String& message, int durationMs) {
    display->setFullWindow();
    display->firstPage();
    do {
        display->fillScreen(GxEPD_WHITE);
        display->setFont(&FreeSansBold12pt7b);
        display->setCursor(10, 25);
        display->print(title);
        
        // Draw message with line breaks
        display->setFont(&FreeSans9pt7b);
        int y = 50;
        int lineHeight = 18;
        String line = "";
        
        for (int i = 0; i < message.length(); i++) {
            char c = message.charAt(i);
            if (c == '\n') {
                // Print current line
                display->setCursor(10, y);
                display->print(line);
                line = "";
                y += lineHeight;
            } else {
                line += c;
            }
        }
        
        // Print last line
        if (line.length() > 0) {
            display->setCursor(10, y);
            display->print(line);
        }
    } while (display->nextPage());
    
    if (durationMs > 0) {
        delay(durationMs);
    }
}

void UI::clear() {
    display->fillScreen(GxEPD_WHITE);
}

void UI::showPoweringDown() {
    setState(STATE_POWERING_DOWN);
    update();
}

void UI::showSleepScreen() {
    setState(STATE_SLEEPING);
    display->setFullWindow();
    display->firstPage();
    do {
        drawSleeping();
    } while (display->nextPage());
}

void UI::showNappingScreen(float batteryVoltage) {
    setState(STATE_SLEEPING);
    display->setFullWindow();
    display->firstPage();
    do {
        display->fillScreen(GxEPD_WHITE);
        
        display->setFont(&FreeSansBold12pt7b);
        display->setCursor(20, 30);
        display->print("SmolTxt Napping");
        
        display->setFont(&FreeSans9pt7b);
        display->setCursor(5, 60);
        display->print("Wake every 15 min to");
        display->setCursor(5, 80);
        display->print("check messages & alert");
        display->setCursor(5, 100);
        display->print("Press any key to wake");
        
        // Show battery voltage in corner
        display->setFont();
        String voltageStr = String(batteryVoltage, 2) + "V";
        display->setCursor(240, 5);
        display->print(voltageStr);
    } while (display->nextPage());
}

void UI::showLowBatteryScreen(float batteryVoltage) {
    setState(STATE_SLEEPING);
    display->setFullWindow();
    display->firstPage();
    do {
        display->fillScreen(GxEPD_WHITE);
        
        display->setFont(&FreeSansBold12pt7b);
        display->setCursor(10, 35);
        display->print("Battery Too Low!");
        
        display->setFont(&FreeSans9pt7b);
        display->setCursor(5, 65);
        display->print("SmolTxt going to sleep");
        display->setCursor(5, 90);
        display->print("Please charge me!");
        
        // Show battery voltage prominently
        display->setFont(&FreeSansBold12pt7b);
        display->setCursor(80, 118);
        display->print(String(batteryVoltage, 2) + "V");
    } while (display->nextPage());
}

void UI::drawPoweringDown() {
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(50, 60);
    display->print("Powering Down...");
}

void UI::drawSleeping() {
    display->fillScreen(GxEPD_WHITE);
    
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(20, 40);
    display->print("SmolTxt Sleeping");
    
    display->setFont(&FreeSans9pt7b);
    display->setCursor(10, 70);
    display->print("Hold Tab 3s to sleep");
    display->setCursor(10, 95);
    display->print("Press reset to wake");
}

// Battery display
void UI::setBatteryStatus(float voltage, int percent) {
    batteryVoltage = voltage;
    batteryPercent = percent;
}

void UI::drawBatteryIcon(int x, int y, int percent) {
    // Battery outline (20x10 pixels)
    int width = 20;
    int height = 10;
    int tipWidth = 2;
    int tipHeight = 4;
    
    // Draw voltage text to the left of battery icon (small font, moved down 2px)
    display->setFont();
    String voltageStr = String(batteryVoltage, 1);  // 1 decimal place
    display->setCursor(x - voltageStr.length() * 6 - 2, y + 2);  // Position to left, down 2px from icon
    display->print(voltageStr);
    
    // Draw battery body
    display->drawRect(x, y, width, height, GxEPD_BLACK);
    
    // Draw battery tip
    display->fillRect(x + width, y + (height - tipHeight) / 2, tipWidth, tipHeight, GxEPD_BLACK);
    
    // Fill battery based on percentage
    if (percent > 0) {
        int fillWidth = ((width - 4) * percent) / 100;  // -4 for 2px margins
        if (fillWidth > 0) {
            display->fillRect(x + 2, y + 2, fillWidth, height - 4, GxEPD_BLACK);
        }
    }
}

