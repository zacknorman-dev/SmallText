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
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(80, 50);
    display->print("SmolTxt");
    
    display->setFont(&FreeSans9pt7b);
    display->setCursor(70, 80);
    display->print("Safe Texting");
    display->setCursor(80, 100);
    display->print("for Kids");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
}

void UI::drawVillageSelect() {
    // Title
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(20, 20);
    display->print("SELECT VILLAGE");
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 45;
    int lineHeight = 18;
    int item = 0;
    
    // List all saved villages from slots 0-9
    for (int slot = 0; slot < 10; slot++) {
        String villageName = Village::getVillageNameFromSlot(slot);
        if (villageName.length() > 0) {
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
            item++;
            
            // Stop if we're out of screen space
            if (y > 100) break;
        }
    }
    
    // New Village
    if (menuSelection == item) {
        display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("New Village");
    if (menuSelection == item) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    item++;
    
    // Join Village
    if (menuSelection == item) {
        display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Join Village");
    if (menuSelection == item) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    item++;
    
    // WiFi & Updates
    if (menuSelection == item) {
        display->fillRect(5, y - 13, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("WiFi & Updates");
    if (menuSelection == item) {
        display->setTextColor(GxEPD_BLACK);
    }
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    // Footer hint
    display->setFont();
    display->setCursor(5, SCREEN_HEIGHT - 8);
    display->print("UP/DN ENTER:ok BS:delete");
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
    // Title
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(60, 20);
    display->print("VILLAGE MENU");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 45;
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
    
    // Delete/Leave Village - text changes based on ownership but that's handled by caller
    if (menuSelection == 3) {
        display->fillRect(5, y - 15, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Leave Village");
    if (menuSelection == 3) {
        display->setTextColor(GxEPD_BLACK);
    }
    
    // Footer hint
    display->setFont();
    display->setCursor(5, SCREEN_HEIGHT - 8);
    display->print("<-:back UP/DN:select ENTER:choose");
}

void UI::drawWiFiSetupMenu() {
    // Title
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(60, 20);
    display->print("WiFi Setup");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    display->setFont(&FreeSans9pt7b);
    
    int y = 50;
    int lineHeight = 25;
    
    // Configure WiFi
    if (menuSelection == 0) {
        display->fillRect(5, y - 18, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Configure WiFi");
    if (menuSelection == 0) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    
    // Check Connection
    if (menuSelection == 1) {
        display->fillRect(5, y - 18, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Check Connection");
    if (menuSelection == 1) {
        display->setTextColor(GxEPD_BLACK);
    }
    y += lineHeight;
    
    // Check for Updates
    if (menuSelection == 2) {
        display->fillRect(5, y - 18, SCREEN_WIDTH - 10, lineHeight, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE);
    }
    display->setCursor(10, y);
    display->print("Check for Updates");
    if (menuSelection == 2) {
        display->setTextColor(GxEPD_BLACK);
    }
    
    // Footer hint
    display->setFont();
    display->setCursor(5, SCREEN_HEIGHT - 8);
    display->print("<-:back UP/DN:select ENTER:choose");
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
    display->setFont(&FreeSansBold12pt7b);
    display->setCursor(40, 20);
    display->print("VILLAGE CHAT");
    
    // Battery icon in upper right
    drawBatteryIcon(SCREEN_WIDTH - 25, 5, batteryPercent);
    
    // Draw divider line
    display->drawLine(0, 25, SCREEN_WIDTH, 25, GxEPD_BLACK);
    
    display->setFont(&FreeSans9pt7b);
    
    int lineHeight = 16;
    int maxVisibleMessages = 3;  // Show 3 messages (reduced to make room for input)
    
    // Calculate input area position (fixed at bottom)
    int inputY = SCREEN_HEIGHT - 35;
    
    // Messages should be positioned just above the input area, growing upward
    // Bottom message is at inputY - 10, then work upward
    int bottomMessageY = inputY - 10;
    
    if (messageHistory.size() == 0) {
        // Center "No messages yet" in the available space
        display->setCursor(10, 60);
        display->print("No messages yet");
    } else {
        // Calculate which messages to display (show most recent at bottom)
        int msgCount = (int)messageHistory.size();
        int endIdx = msgCount - 1;  // Most recent message
        
        if (msgCount > maxVisibleMessages) {
            endIdx = msgCount - 1 - messageScrollOffset;
            // Bounds checking
            if (endIdx >= msgCount) endIdx = msgCount - 1;
            if (endIdx < 0) endIdx = maxVisibleMessages - 1;
            if (endIdx >= msgCount) endIdx = msgCount - 1;  // Double check after adjustment
        }
        
        int startIdx = endIdx - maxVisibleMessages + 1;
        if (startIdx < 0) startIdx = 0;
        if (startIdx >= msgCount) startIdx = msgCount - 1;
        
        // Final safety check
        if (endIdx < 0 || endIdx >= msgCount || startIdx < 0 || startIdx >= msgCount) {
            Serial.println("[UI] ERROR: Invalid message indices!");
            display->setCursor(10, 60);
            display->print("Display error");
            return;
        }
        
        // Count how many messages we'll actually display
        int numMessages = endIdx - startIdx + 1;
        
        // Start from the bottom and work upward
        int y = bottomMessageY - (lineHeight * (numMessages - 1));
        
        // Display messages from oldest to newest (bottom to top positioning)
        for (int i = startIdx; i <= endIdx && i < messageHistory.size(); i++) {
            const Message& msg = messageHistory[i];
            
            // Format: "You: " or "Alice: "
            display->setCursor(5, y);
            // Check if this message is from the current user
            if (msg.sender == currentUsername) {
                display->print("You");
            } else {
                // Truncate long sender names
                String sender = msg.sender;
                if (sender.length() > 8) {
                    sender = sender.substring(0, 8);
                }
                display->print(sender);
            }
            display->print(": ");
            
            // Message content (truncate if too long)
            String content = msg.content;
            if (content.length() > 30) {
                content = content.substring(0, 27) + "...";
            }
            display->print(content);
            
            // Show status for sent messages (not received)
            if (!msg.received) {
                display->print(" ");
                display->setFont();  // Use smaller font for status
                switch (msg.status) {
                    case MSG_SENT:
                        display->print("(Sent)");
                        break;
                    case MSG_RECEIVED:
                        display->print("(Rec'd)");
                        break;
                    case MSG_SEEN:
                        display->print("(Rec'd)");  // Same as received for now
                        break;
                    case MSG_READ:
                        display->print("(Read)");
                        break;
                }
                display->setFont(&FreeSans9pt7b);  // Restore font
            }
            
            y += lineHeight;  // Next message goes down (newer)
        }
    }
    
    // Draw input area at bottom (inputY already defined above)
    display->drawLine(0, inputY - 5, SCREEN_WIDTH, inputY - 5, GxEPD_BLACK);
    
    // Input prompt
    display->setFont(&FreeSans9pt7b);
    display->setCursor(5, inputY + 10);
    display->print(">");
    
    // Show current input text
    display->setCursor(15, inputY + 10);
    String displayText = inputText;
    if (displayText.length() > 32) {
        displayText = displayText.substring(displayText.length() - 32);
    }
    display->print(displayText);
    
    // Cursor indicator
    display->print("_");
    
    // Footer hint
    display->setFont();
    display->setCursor(5, SCREEN_HEIGHT - 4);
    display->print("<-:back UP/DN:scroll ENTER:send");
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
        case STATE_VILLAGE_MENU:
            maxItems = 3;
            break;
        case STATE_WIFI_SETUP_MENU:
            maxItems = 2;  // Configure WiFi, Check Connection, Check for Updates (0-2)
            break;
        default:
            maxItems = 0;
    }
    
    if (menuSelection < maxItems) {
        menuSelection++;
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
    Serial.println("[UI] Message added. Total: " + String(messageHistory.size()));
}

void UI::clearMessages() {
    messageHistory.clear();
}

void UI::scrollMessagesUp() {
    if (messageScrollOffset > 0) {
        messageScrollOffset--;
    }
}

void UI::scrollMessagesDown() {
    messageScrollOffset++;
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
    
    // Draw voltage text to the left of battery icon (small font)
    display->setFont();
    String voltageStr = String(batteryVoltage, 1);  // 1 decimal place
    display->setCursor(x - voltageStr.length() * 6 - 2, y + 8);  // Position to left
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

