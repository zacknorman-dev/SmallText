#ifndef WIFIMANAGER_H
#define WIFIMANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

enum WiFiConnectionState {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
};

class WiFiManager {
public:
    WiFiManager();
    
    // Initialize WiFi system
    bool begin();
    
    // Credential management
    bool hasCredentials();
    bool saveCredentials(const String& ssid, const String& password);
    void clearCredentials();
    String getSavedSSID();
    
    // Connection management
    bool connect();
    bool connectWithCredentials(const String& ssid, const String& password);
    void disconnect();
    bool isConnected();
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

private:
    Preferences prefs;
    WiFiConnectionState state;
    bool autoReconnect;
    unsigned long lastReconnectAttempt;
    unsigned long reconnectInterval;
    int connectionAttempts;
    
    static const int MAX_CONNECTION_ATTEMPTS = 3;
    static const unsigned long CONNECTION_TIMEOUT = 10000; // 10 seconds
    
    void updateState();
};

#endif
