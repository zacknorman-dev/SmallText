#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>
#include <vector>

enum WiFiConnectionState {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
};

struct SavedNetwork {
    String ssid;
    String password;
};

struct ScannedNetwork {
    String ssid;
    int rssi;          // Signal strength in dBm
    bool encrypted;    // Is network password-protected?
    bool saved;        // Is this network in our saved list?
};

class WiFiManager {
public:
    WiFiManager();
    
    // Initialize WiFi system
    bool begin();
    
    // Multi-network credential management
    bool saveNetwork(const String& ssid, const String& password);
    bool removeNetwork(const String& ssid);
    bool hasNetwork(const String& ssid);
    std::vector<SavedNetwork> getSavedNetworks();
    int getSavedNetworkCount();
    
    // Legacy single-network support (for backward compatibility)
    bool hasCredentials();
    bool saveCredentials(const String& ssid, const String& password);
    void clearCredentials();
    String getSavedSSID();
    
    // Network scanning
    std::vector<ScannedNetwork> scanNetworks();
    int getScannedNetworkCount();
    
    // Connection management (now with waterfall support)
    bool connect();  // Try to connect to any saved network (waterfall)
    bool connectToNetwork(const String& ssid);  // Connect to specific saved network
    bool connectWithCredentials(const String& ssid, const String& password);
    void disconnect();
    bool isConnected();
    String getConnectedSSID();
    WiFiConnectionState getState();
    
    // Status info
    String getIPAddress();
    int getSignalStrength();  // Returns RSSI in dBm
    String getStatusString();
    
    // Periodic check (call in loop)
    void update();
    
    // Reconnection settings
    void setAutoReconnect(bool enabled);
    void setReconnectInterval(unsigned long intervalMs);
    
    // Time synchronization
    bool syncNTPTime();
    unsigned long getLastNTPSync() const;
    long getTimeOffset() const;

private:
    void configureNTP();
    bool connectToSavedNetwork(const SavedNetwork& network);
    void saveLastConnectedSSID(const String& ssid);
    String getLastConnectedSSID();
    
    Preferences prefs;
    WiFiConnectionState state;
    bool autoReconnect;
    unsigned long lastReconnectAttempt;
    unsigned long reconnectInterval;
    int connectionAttempts;
    
    // Multi-network storage
    std::vector<SavedNetwork> savedNetworks;
    std::vector<ScannedNetwork> scannedNetworks;
    void loadSavedNetworks();
    void saveSavedNetworks();
    
    // Time tracking
    unsigned long lastNTPSync;  // millis() when last NTP sync occurred
    long timeOffset;  // Offset to add to millis() to get Unix timestamp
    
    static const int MAX_CONNECTION_ATTEMPTS = 3;
    static const unsigned long CONNECTION_TIMEOUT = 10000; // 10 seconds
    static const int MAX_SAVED_NETWORKS = 10;  // Limit to prevent memory issues
    
    void updateState();
};

#endif
