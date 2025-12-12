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
    
    // Load saved networks from preferences
    loadSavedNetworks();
    
    Serial.println("[WiFi] WiFiManager initialized with " + String(savedNetworks.size()) + " saved networks");
    return true;
}

// Load saved networks from Preferences
void WiFiManager::loadSavedNetworks() {
    savedNetworks.clear();
    
    int count = prefs.getInt("count", 0);
    Serial.println("[WiFi] Loading " + String(count) + " saved networks");
    
    for (int i = 0; i < count && i < MAX_SAVED_NETWORKS; i++) {
        String ssidKey = "ssid" + String(i);
        String passKey = "pass" + String(i);
        
        String ssid = prefs.getString(ssidKey.c_str(), "");
        String password = prefs.getString(passKey.c_str(), "");
        
        if (ssid.length() > 0) {
            SavedNetwork net;
            net.ssid = ssid;
            net.password = password;
            savedNetworks.push_back(net);
            Serial.println("[WiFi]   - " + ssid);
        }
    }
}

// Save all networks to Preferences
void WiFiManager::saveSavedNetworks() {
    // Clear old entries first
    prefs.clear();
    
    int count = min((int)savedNetworks.size(), MAX_SAVED_NETWORKS);
    prefs.putInt("count", count);
    
    Serial.println("[WiFi] Saving " + String(count) + " networks");
    
    for (int i = 0; i < count; i++) {
        String ssidKey = "ssid" + String(i);
        String passKey = "pass" + String(i);
        
        prefs.putString(ssidKey.c_str(), savedNetworks[i].ssid);
        prefs.putString(passKey.c_str(), savedNetworks[i].password);
        Serial.println("[WiFi]   - " + savedNetworks[i].ssid);
    }
}

// Save a network (add to list or update existing)
bool WiFiManager::saveNetwork(const String& ssid, const String& password) {
    if (ssid.length() == 0) {
        Serial.println("[WiFi] Cannot save empty SSID");
        return false;
    }
    
    // Check if network already exists - update it
    for (auto& net : savedNetworks) {
        if (net.ssid == ssid) {
            net.password = password;
            Serial.println("[WiFi] Updated network: " + ssid);
            saveSavedNetworks();
            return true;
        }
    }
    
    // Add new network
    if (savedNetworks.size() >= MAX_SAVED_NETWORKS) {
        Serial.println("[WiFi] Maximum networks reached (" + String(MAX_SAVED_NETWORKS) + ")");
        return false;
    }
    
    SavedNetwork net;
    net.ssid = ssid;
    net.password = password;
    savedNetworks.push_back(net);
    
    Serial.println("[WiFi] Saved new network: " + ssid);
    saveSavedNetworks();
    return true;
}

// Remove a network from saved list
bool WiFiManager::removeNetwork(const String& ssid) {
    for (auto it = savedNetworks.begin(); it != savedNetworks.end(); ++it) {
        if (it->ssid == ssid) {
            Serial.println("[WiFi] Removing network: " + ssid);
            savedNetworks.erase(it);
            saveSavedNetworks();
            return true;
        }
    }
    
    Serial.println("[WiFi] Network not found: " + ssid);
    return false;
}

// Check if a specific network is saved
bool WiFiManager::hasNetwork(const String& ssid) {
    for (const auto& net : savedNetworks) {
        if (net.ssid == ssid) {
            return true;
        }
    }
    return false;
}

// Get all saved networks
std::vector<SavedNetwork> WiFiManager::getSavedNetworks() {
    return savedNetworks;
}

// Get count of saved networks
int WiFiManager::getSavedNetworkCount() {
    return savedNetworks.size();
}

// Scan for available WiFi networks
std::vector<ScannedNetwork> WiFiManager::scanNetworks() {
    scannedNetworks.clear();
    
    Serial.println("[WiFi] Scanning for networks...");
    int n = WiFi.scanNetworks();
    
    if (n == 0) {
        Serial.println("[WiFi] No networks found");
        return scannedNetworks;
    }
    
    Serial.println("[WiFi] Found " + String(n) + " networks:");
    
    // Build list, filtering duplicates (keep strongest signal)
    for (int i = 0; i < n; i++) {
        ScannedNetwork net;
        net.ssid = WiFi.SSID(i);
        net.rssi = WiFi.RSSI(i);
        net.encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
        net.saved = hasNetwork(net.ssid);
        
        // Check if we already have this SSID
        bool found = false;
        for (auto& existing : scannedNetworks) {
            if (existing.ssid == net.ssid) {
                // Keep the one with stronger signal
                if (net.rssi > existing.rssi) {
                    existing.rssi = net.rssi;
                    existing.encrypted = net.encrypted;
                }
                found = true;
                break;
            }
        }
        
        if (!found) {
            scannedNetworks.push_back(net);
        }
        
        Serial.print("[WiFi]   ");
        Serial.print(i + 1);
        Serial.print(": ");
        Serial.print(net.ssid);
        Serial.print(" (");
        Serial.print(net.rssi);
        Serial.print(" dBm) ");
        Serial.print(net.encrypted ? "[Secured]" : "[Open]");
        Serial.println(net.saved ? " [Saved]" : "");
    }
    
    return scannedNetworks;
}

int WiFiManager::getScannedNetworkCount() {
    return scannedNetworks.size();
}

// Legacy methods for backward compatibility
bool WiFiManager::hasCredentials() {
    return savedNetworks.size() > 0;
}

bool WiFiManager::saveCredentials(const String& ssid, const String& password) {
    return saveNetwork(ssid, password);
}

void WiFiManager::clearCredentials() {
    savedNetworks.clear();
    saveSavedNetworks();
    Serial.println("[WiFi] All credentials cleared");
}

String WiFiManager::getSavedSSID() {
    if (savedNetworks.size() > 0) {
        return savedNetworks[0].ssid;
    }
    return "";
}

// Save/retrieve last successfully connected SSID
void WiFiManager::saveLastConnectedSSID(const String& ssid) {
    prefs.putString("last", ssid);
}

String WiFiManager::getLastConnectedSSID() {
    return prefs.getString("last", "");
}

// Waterfall connection: try last successful, then all saved networks
bool WiFiManager::connect() {
    if (savedNetworks.size() == 0) {
        Serial.println("[WiFi] No saved networks");
        state = WIFI_FAILED;
        return false;
    }
    
    // Try last successful network first for faster reconnect
    String lastSSID = getLastConnectedSSID();
    if (lastSSID.length() > 0) {
        Serial.println("[WiFi] Trying last connected network: " + lastSSID);
        if (connectToNetwork(lastSSID)) {
            return true;
        }
    }
    
    // Try all saved networks in order
    Serial.println("[WiFi] Waterfall connecting through " + String(savedNetworks.size()) + " saved networks");
    
    for (const auto& net : savedNetworks) {
        if (net.ssid == lastSSID) continue;  // Already tried this one
        
        Serial.println("[WiFi] Trying: " + net.ssid);
        if (connectToSavedNetwork(net)) {
            return true;
        }
    }
    
    Serial.println("[WiFi] Failed to connect to any saved network");
    state = WIFI_FAILED;
    return false;
}

// Connect to a specific saved network by SSID
bool WiFiManager::connectToNetwork(const String& ssid) {
    for (const auto& net : savedNetworks) {
        if (net.ssid == ssid) {
            return connectToSavedNetwork(net);
        }
    }
    
    Serial.println("[WiFi] Network not in saved list: " + ssid);
    return false;
}

// Internal: connect to a SavedNetwork struct
bool WiFiManager::connectToSavedNetwork(const SavedNetwork& network) {
    bool success = connectWithCredentials(network.ssid, network.password);
    if (success) {
        saveLastConnectedSSID(network.ssid);
    }
    return success;
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

String WiFiManager::getConnectedSSID() {
    if (isConnected()) {
        return WiFi.SSID();
    }
    return "";
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
