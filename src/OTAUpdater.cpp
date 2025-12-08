#include "OTAUpdater.h"
#include <Update.h>
#include <esp_ota_ops.h>

OTAUpdater* OTAUpdater::instance = nullptr;

OTAUpdater::OTAUpdater() {
    logger = nullptr;
    status = UPDATE_IDLE;
    currentVersion = FIRMWARE_VERSION;
    latestVersion = "";
    downloadURL = "";
    releaseNotes = "";
    githubOwner = "";
    githubRepo = "";
    customURL = "";
    progressCallback = nullptr;
    instance = this;
}

bool OTAUpdater::begin(Logger* loggerInstance) {
    logger = loggerInstance;
    
    // Log partition information for debugging
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* update = esp_ota_get_next_update_partition(NULL);
    
    Serial.println("[OTA] ====================================");
    Serial.println("[OTA] Partition Information:");
    if (running) {
        Serial.println("[OTA] Running partition: " + String(running->label));
        Serial.println("[OTA] Running partition size: " + String(running->size / 1024) + " KB");
    }
    if (update) {
        Serial.println("[OTA] Update partition: " + String(update->label));
        Serial.println("[OTA] Update partition size: " + String(update->size / 1024) + " KB");
    } else {
        Serial.println("[OTA] WARNING: No OTA update partition found!");
        Serial.println("[OTA] OTA updates may not work correctly.");
    }
    Serial.println("[OTA] Firmware version: " + currentVersion);
    Serial.println("[OTA] ====================================");
    
    if (logger) {
        logger->info("OTA Updater initialized, version: " + currentVersion);
    }
    
    return true;
}

void OTAUpdater::setGitHubRepo(const String& owner, const String& repo) {
    githubOwner = owner;
    githubRepo = repo;
    
    if (logger) {
        logger->info("OTA source: GitHub " + owner + "/" + repo);
    }
    Serial.println("[OTA] Update source: GitHub " + owner + "/" + repo);
}

void OTAUpdater::setCustomURL(const String& url) {
    customURL = url;
    
    if (logger) {
        logger->info("OTA source: " + url);
    }
    Serial.println("[OTA] Update source: " + url);
}

bool OTAUpdater::checkForUpdate() {
    if (WiFi.status() != WL_CONNECTED) {
        if (logger) logger->error("OTA check failed: no WiFi");
        Serial.println("[OTA] No WiFi connection");
        status = UPDATE_FAILED;
        return false;
    }
    
    status = UPDATE_CHECKING;
    Serial.println("[OTA] Checking for updates...");
    
    if (githubOwner.length() > 0 && githubRepo.length() > 0) {
        return fetchLatestRelease();
    } else if (customURL.length() > 0) {
        // For custom URL, assume URL points directly to firmware.bin
        // and a version.json file exists at same location
        downloadURL = customURL;
        latestVersion = "custom";
        status = UPDATE_AVAILABLE;
        return true;
    }
    
    if (logger) logger->error("OTA check failed: no update source configured");
    Serial.println("[OTA] No update source configured");
    status = UPDATE_FAILED;
    return false;
}

bool OTAUpdater::fetchLatestRelease() {
    HTTPClient http;
    String apiURL = "https://api.github.com/repos/" + githubOwner + "/" + githubRepo + "/releases/latest";
    
    Serial.println("[OTA] Fetching: " + apiURL);
    http.begin(apiURL);
    http.addHeader("User-Agent", "SmolTxt-OTA");
    
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        if (logger) logger->error("OTA GitHub API failed, code=" + String(httpCode));
        Serial.println("[OTA] GitHub API failed, code: " + String(httpCode));
        http.end();
        status = UPDATE_FAILED;
        return false;
    }
    
    String payload = http.getString();
    http.end();
    
    // Parse JSON response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        if (logger) logger->error("OTA JSON parse failed");
        Serial.println("[OTA] Failed to parse GitHub response");
        status = UPDATE_FAILED;
        return false;
    }
    
    // Extract version and download URL
    latestVersion = doc["tag_name"].as<String>();
    releaseNotes = doc["body"].as<String>();
    
    // Find firmware.bin asset
    JsonArray assets = doc["assets"];
    for (JsonVariant asset : assets) {
        String assetName = asset["name"].as<String>();
        if (assetName.endsWith(".bin")) {
            downloadURL = asset["browser_download_url"].as<String>();
            Serial.println("[OTA] Download URL: " + downloadURL);
            break;
        }
    }
    
    if (downloadURL.length() == 0) {
        if (logger) logger->error("OTA: no .bin file in release");
        Serial.println("[OTA] No firmware.bin found in release");
        status = UPDATE_FAILED;
        return false;
    }
    
    // Compare versions
    Serial.println("[OTA] Current: " + currentVersion + ", Latest: " + latestVersion);
    
    if (compareVersions(latestVersion, currentVersion)) {
        status = UPDATE_AVAILABLE;
        if (logger) logger->info("OTA: Update available " + latestVersion);
        Serial.println("[OTA] Update available: " + latestVersion);
        return true;
    } else {
        status = UPDATE_NO_UPDATE;
        if (logger) logger->info("OTA: Already on latest version");
        Serial.println("[OTA] Already on latest version");
        return false;
    }
}

bool OTAUpdater::compareVersions(const String& v1, const String& v2) {
    // Simple string comparison for now
    // Format: v0.13.0 vs v0.12.0
    // Remove 'v' prefix if present
    String ver1 = v1;
    String ver2 = v2;
    if (ver1.startsWith("v")) ver1 = ver1.substring(1);
    if (ver2.startsWith("v")) ver2 = ver2.substring(1);
    
    // Split by dots and compare
    int v1_parts[3] = {0, 0, 0};
    int v2_parts[3] = {0, 0, 0};
    
    sscanf(ver1.c_str(), "%d.%d.%d", &v1_parts[0], &v1_parts[1], &v1_parts[2]);
    sscanf(ver2.c_str(), "%d.%d.%d", &v2_parts[0], &v2_parts[1], &v2_parts[2]);
    
    // Compare major.minor.patch
    for (int i = 0; i < 3; i++) {
        if (v1_parts[i] > v2_parts[i]) return true;
        if (v1_parts[i] < v2_parts[i]) return false;
    }
    
    return false; // Equal versions
}

bool OTAUpdater::performUpdate() {
    if (status != UPDATE_AVAILABLE) {
        if (logger) logger->error("OTA: No update available to install");
        Serial.println("[OTA] No update available");
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        if (logger) logger->error("OTA: No WiFi for update");
        Serial.println("[OTA] No WiFi connection");
        status = UPDATE_FAILED;
        return false;
    }
    
    status = UPDATE_DOWNLOADING;
    Serial.println("[OTA] ====================================");
    Serial.println("[OTA] Starting OTA Update");
    Serial.println("[OTA] Current version: " + currentVersion);
    Serial.println("[OTA] Target version: " + latestVersion);
    Serial.println("[OTA] Download URL: " + downloadURL);
    Serial.println("[OTA] Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("[OTA] ====================================");
    
    if (logger) logger->info("OTA: Starting update from " + downloadURL);
    
    // Set up progress callback
    if (progressCallback) {
        httpUpdate.onProgress([](int progress, int total) {
            if (instance && instance->progressCallback) {
                instance->progressCallback(progress, total);
            }
        });
    }
    
    // Configure HTTPUpdate for better reliability
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);  // Let HTTPUpdate handle the reboot automatically
    
    // Perform update with better timeout handling
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate validation for GitHub
    client.setTimeout(60000);  // 60 second timeout
    
    // Set larger buffer for faster download
    client.setNoDelay(true);
    
    Serial.println("[OTA] Starting update process...");
    Serial.println("[OTA] This will take a few minutes...");
    
    t_httpUpdate_return ret = httpUpdate.update(client, downloadURL);
    
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            if (logger) logger->error("OTA failed: " + String(httpUpdate.getLastErrorString()));
            Serial.println("[OTA] Update failed: " + String(httpUpdate.getLastErrorString()));
            Serial.println("[OTA] Error code: " + String(httpUpdate.getLastError()));
            status = UPDATE_FAILED;
            return false;
            
        case HTTP_UPDATE_NO_UPDATES:
            if (logger) logger->info("OTA: No update needed");
            Serial.println("[OTA] No update needed");
            status = UPDATE_NO_UPDATE;
            return false;
            
        case HTTP_UPDATE_OK:
            // This code should never execute because rebootOnUpdate(true) will restart
            // But if we get here somehow, manually restart
            if (logger) logger->info("OTA: Update successful, restarting...");
            Serial.println("[OTA] Update successful! Restarting in 2 seconds...");
            status = UPDATE_SUCCESS;
            delay(2000);
            ESP.restart();
            return true;
    }
    
    return false;
}

UpdateStatus OTAUpdater::getStatus() {
    return status;
}

String OTAUpdater::getStatusString() {
    switch (status) {
        case UPDATE_IDLE: return "Idle";
        case UPDATE_CHECKING: return "Checking...";
        case UPDATE_AVAILABLE: return "Update Available";
        case UPDATE_NO_UPDATE: return "Up to Date";
        case UPDATE_DOWNLOADING: return "Downloading...";
        case UPDATE_INSTALLING: return "Installing...";
        case UPDATE_SUCCESS: return "Success!";
        case UPDATE_FAILED: return "Failed";
        default: return "Unknown";
    }
}

String OTAUpdater::getCurrentVersion() {
    return currentVersion;
}

String OTAUpdater::getLatestVersion() {
    return latestVersion;
}

String OTAUpdater::getUpdateURL() {
    return downloadURL;
}

void OTAUpdater::setProgressCallback(void (*callback)(int progress, int total)) {
    progressCallback = callback;
}

void OTAUpdater::updateProgress(int progress, int total) {
    if (instance) {
        Serial.printf("[OTA] Progress: %d/%d bytes\n", progress, total);
    }
}
