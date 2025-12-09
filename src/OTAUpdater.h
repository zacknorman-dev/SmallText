#ifndef OTAUPDATER_H
#define OTAUPDATER_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "Logger.h"

// Firmware version comes from build flags in platformio.ini
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.38.0"
#endif

enum UpdateStatus {
    UPDATE_IDLE,
    UPDATE_CHECKING,
    UPDATE_AVAILABLE,
    UPDATE_NO_UPDATE,
    UPDATE_DOWNLOADING,
    UPDATE_INSTALLING,
    UPDATE_SUCCESS,
    UPDATE_FAILED
};

class OTAUpdater {
public:
    OTAUpdater();
    
    // Initialize updater
    bool begin(Logger* loggerInstance = nullptr);
    
    // Check for updates (non-blocking)
    // Returns true if new version available
    bool checkForUpdate();
    
    // Download and install update (blocking)
    // Returns true if successful (will restart device)
    bool performUpdate();
    
    // Get status
    UpdateStatus getStatus();
    String getStatusString();
    String getCurrentVersion();
    String getLatestVersion();
    String getUpdateURL();
    
    // Configure update source
    void setGitHubRepo(const String& owner, const String& repo);
    void setCustomURL(const String& url);
    
    // Progress callback for UI updates
    void setProgressCallback(void (*callback)(int progress, int total));

private:
    Logger* logger;
    UpdateStatus status;
    String currentVersion;
    String latestVersion;
    String downloadURL;
    String releaseNotes;
    String githubOwner;
    String githubRepo;
    String customURL;
    
    void (*progressCallback)(int progress, int total);
    
    // GitHub API integration
    bool fetchLatestRelease();
    bool compareVersions(const String& v1, const String& v2);
    
    // Update progress callback (static for HTTPUpdate)
    static void updateProgress(int progress, int total);
    static OTAUpdater* instance; // For static callback
};

#endif
