#include "WiFiManager.h"

WiFiManager::WiFiManager() {
    state = WIFI_DISCONNECTED;
    autoReconnect = true;
    lastReconnectAttempt = 0;
    reconnectInterval = 30000; // 30 seconds between reconnect attempts
    connectionAttempts = 0;
    lastNTPSync = 0;
    timeOffset = 0;
}

bool WiFiManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // We'll handle reconnection ourselves
    
    // Open preferences for WiFi credentials
    if (!prefs.begin("wifi", false)) {
        Serial.println("[WiFi] Failed to initialize preferences");
        return false;
    }
    
    Serial.println("[WiFi] WiFiManager initialized");
    return true;
}

bool WiFiManager::hasCredentials() {
    String ssid = prefs.getString("ssid", "");
    return ssid.length() > 0;
}

bool WiFiManager::saveCredentials(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        Serial.println("[WiFi] Cannot save empty SSID");
        return false;
    }
    
    prefs.putString("ssid", ssid);
    prefs.putString("password", password);
    
    Serial.println("[WiFi] Credentials saved for SSID: " + ssid);
    return true;
}

void WiFiManager::clearCredentials() {
    prefs.clear();
    Serial.println("[WiFi] Credentials cleared");
}

String WiFiManager::getSavedSSID() {
    return prefs.getString("ssid", "");
}

bool WiFiManager::connect() {
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
    
    if (ssid.length() == 0) {
        Serial.println("[WiFi] No saved credentials");
        state = WIFI_FAILED;
        return false;
    }
    
    return connectWithCredentials(ssid, password);
}

bool WiFiManager::connectWithCredentials(const String& ssid, const String& password) {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Already connected");
        state = WIFI_CONNECTED;
        return true;
    }
    
    Serial.println("[WiFi] Connecting to: " + ssid);
    state = WIFI_CONNECTING;
    connectionAttempts = 0;
    
    WiFi.begin(ssid.c_str(), password.c_str());
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < CONNECTION_TIMEOUT) {
        delay(500);
        Serial.print(".");
        connectionAttempts++;
        
        if (connectionAttempts > MAX_CONNECTION_ATTEMPTS * 10) {
            break;
        }
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        state = WIFI_CONNECTED;
        Serial.println("[WiFi] Connected!");
        Serial.println("[WiFi] IP: " + WiFi.localIP().toString());
        Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
        
        // Sync NTP time after successful connection
        syncNTPTime();
        
        return true;
    }
    
    state = WIFI_FAILED;
    Serial.println("[WiFi] Connection failed");
    return false;
}

void WiFiManager::disconnect() {
    WiFi.disconnect(true);
    state = WIFI_DISCONNECTED;
    Serial.println("[WiFi] Disconnected");
}

bool WiFiManager::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

WiFiConnectionState WiFiManager::getState() {
    return state;
}

String WiFiManager::getIPAddress() {
    if (isConnected()) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

int WiFiManager::getSignalStrength() {
    if (isConnected()) {
        return WiFi.RSSI();
    }
    return -100; // No signal
}

String WiFiManager::getStatusString() {
    switch (state) {
        case WIFI_DISCONNECTED:
            return "Disconnected";
        case WIFI_CONNECTING:
            return "Connecting...";
        case WIFI_CONNECTED:
            return "Connected";
        case WIFI_FAILED:
            return "Failed";
        default:
            return "Unknown";
    }
}

void WiFiManager::update() {
    updateState();
    
    // Auto-reconnect if enabled and disconnected
    if (autoReconnect && state != WIFI_CONNECTED && state != WIFI_CONNECTING) {
        if (millis() - lastReconnectAttempt > reconnectInterval) {
            lastReconnectAttempt = millis();
            Serial.println("[WiFi] Auto-reconnecting...");
            connect();
        }
    }
    
    // Periodic NTP sync (every 24 hours)
    if (state == WIFI_CONNECTED && lastNTPSync > 0) {
        unsigned long timeSinceLastSync = millis() - lastNTPSync;
        const unsigned long SYNC_INTERVAL = 24UL * 60UL * 60UL * 1000UL;  // 24 hours in ms
        
        if (timeSinceLastSync > SYNC_INTERVAL) {
            Serial.println("[WiFi] 24 hour NTP re-sync");
            syncNTPTime();
        }
    }
}

void WiFiManager::updateState() {
    if (WiFi.status() == WL_CONNECTED && state != WIFI_CONNECTED) {
        state = WIFI_CONNECTED;
        Serial.println("[WiFi] Connection established");
    } else if (WiFi.status() != WL_CONNECTED && state == WIFI_CONNECTED) {
        state = WIFI_DISCONNECTED;
        Serial.println("[WiFi] Connection lost");
    }
}

void WiFiManager::setAutoReconnect(bool enabled) {
    autoReconnect = enabled;
    Serial.println("[WiFi] Auto-reconnect: " + String(enabled ? "enabled" : "disabled"));
}

void WiFiManager::setReconnectInterval(unsigned long intervalMs) {
    reconnectInterval = intervalMs;
    Serial.println("[WiFi] Reconnect interval set to: " + String(intervalMs) + "ms");
}

void WiFiManager::configureNTP() {
    // Configure NTP with pool servers and timezone offset
    // UTC-5 for EST, or 0 for UTC (adjust as needed)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
    Serial.println("[WiFi] NTP configured");
}

bool WiFiManager::syncNTPTime() {
    if (!isConnected()) {
        Serial.println("[WiFi] Cannot sync NTP - not connected");
        return false;
    }
    
    Serial.println("[WiFi] Syncing NTP time...");
    configureNTP();
    
    // Wait for time to be set (with timeout)
    int attempts = 0;
    const int MAX_ATTEMPTS = 20;  // 10 seconds total
    
    while (attempts < MAX_ATTEMPTS) {
        time_t now = time(nullptr);
        if (now > 1000000000) {  // Valid Unix timestamp (after year 2001)
            // Calculate offset between millis() and Unix time
            unsigned long currentMillis = millis();
            timeOffset = now - (currentMillis / 1000);
            lastNTPSync = currentMillis;
            
            // Convert to readable time for logging
            struct tm timeinfo;
            gmtime_r(&now, &timeinfo);
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
            
            Serial.println("[WiFi] NTP sync successful: " + String(timeStr));
            Serial.println("[WiFi] Time offset: " + String(timeOffset) + " seconds");
            return true;
        }
        delay(500);
        attempts++;
    }
    
    Serial.println("[WiFi] NTP sync timeout");
    return false;
}

unsigned long WiFiManager::getLastNTPSync() const {
    return lastNTPSync;
}

long WiFiManager::getTimeOffset() const {
    return timeOffset;
}
