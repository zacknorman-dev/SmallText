#include "Logger.h"

const char* Logger::LOG_FILE = "/debug.log";

// Global logger instance
Logger logger;

Logger::Logger() {
    currentLevel = LOG_INFO;
    bootTime = 0;
    serialConnected = false;
    deviceMAC = ESP.getEfuseMac();
    
    // Generate debug topic from MAC
    char macStr[13];
    sprintf(macStr, "%012llx", deviceMAC);
    debugTopic = "smoltxt/" + String(macStr) + "/debug";
}

bool Logger::begin() {
    bootTime = millis();
    
    if (!LittleFS.begin(true)) {
        Serial.println(F("[Logger] ERROR: Failed to mount LittleFS"));
        return false;
    }
    
    // Load existing log from file
    loadBufferFromFile();
    
    // Log boot event
    logBoot();
    
    Serial.println(F("[Logger] Initialized"));
    return true;
}

void Logger::setLogLevel(LogLevel level) {
    currentLevel = level;
}

void Logger::log(LogLevel level, const String& message) {
    // Filter by log level
    if (level < currentLevel) {
        return;
    }
    
    // Create log entry
    LogEntry entry;
    entry.timestamp = millis();
    entry.level = level;
    entry.message = message;
    
    // Add to buffer
    logBuffer.push_back(entry);
    
    // Also output to Serial immediately for real-time monitoring
    Serial.print("[");
    Serial.print(entry.timestamp);
    Serial.print("] ");
    Serial.print(levelToString(level));
    Serial.print(": ");
    Serial.println(message);
    
    // Prune if buffer is too large
    if (logBuffer.size() > MAX_ENTRIES) {
        pruneOldEntries();
    }
    
    // Periodically write to file (every 10 entries to reduce flash wear)
    if (logBuffer.size() % 10 == 0) {
        writeBufferToFile();
    }
}

void Logger::debug(const String& message) {
    log(LOG_DEBUG, message);
}

void Logger::info(const String& message) {
    log(LOG_INFO, message);
}

void Logger::error(const String& message) {
    log(LOG_ERROR, message);
}

void Logger::critical(const String& message) {
    log(LOG_CRITICAL, message);
}

void Logger::logBoot() {
    info("======== SYSTEM BOOT ========");
    info("Build: " + String(__DATE__) + " " + String(__TIME__));
}

void Logger::logSessionMarker(const String& marker) {
    info("===== " + marker + " =====");
}

void Logger::pruneOldEntries() {
    if (logBuffer.size() <= PRUNE_COUNT) {
        return;
    }
    
    // Remove oldest 25% of entries
    logBuffer.erase(logBuffer.begin(), logBuffer.begin() + PRUNE_COUNT);
    
    info("Log buffer pruned, removed " + String(PRUNE_COUNT) + " old entries");
}

void Logger::writeBufferToFile() {
    File file = LittleFS.open(LOG_FILE, "w");
    if (!file) {
        Serial.println(F("[Logger] ERROR: Failed to open log file for writing"));
        return;
    }
    
    // Write each log entry
    for (const auto& entry : logBuffer) {
        file.print("[");
        file.print(entry.timestamp);
        file.print("] ");
        file.print(levelToString(entry.level));
        file.print(": ");
        file.println(entry.message);
    }
    
    file.close();
}

void Logger::loadBufferFromFile() {
    if (!LittleFS.exists(LOG_FILE)) {
        Serial.println(F("[Logger] No existing log file found"));
        return;
    }
    
    File file = LittleFS.open(LOG_FILE, "r");
    if (!file) {
        Serial.println(F("[Logger] ERROR: Failed to open log file for reading"));
        return;
    }
    
    logBuffer.clear();
    
    // Parse log entries from file
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) continue;
        
        // Parse: [timestamp] LEVEL: message
        int timestampEnd = line.indexOf(']');
        int levelEnd = line.indexOf(':', timestampEnd);
        
        if (timestampEnd > 0 && levelEnd > timestampEnd) {
            LogEntry entry;
            entry.timestamp = line.substring(1, timestampEnd).toInt();
            
            String levelStr = line.substring(timestampEnd + 2, levelEnd);
            levelStr.trim();
            if (levelStr == "DEBUG") entry.level = LOG_DEBUG;
            else if (levelStr == "INFO") entry.level = LOG_INFO;
            else if (levelStr == "ERROR") entry.level = LOG_ERROR;
            else if (levelStr == "CRITICAL") entry.level = LOG_CRITICAL;
            else entry.level = LOG_INFO;
            
            entry.message = line.substring(levelEnd + 1);
            entry.message.trim();
            
            logBuffer.push_back(entry);
        }
    }
    
    file.close();
    
    Serial.print(F("[Logger] Loaded "));
    Serial.print(logBuffer.size());
    Serial.println(F(" log entries from file"));
}

String Logger::levelToString(LogLevel level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO: return "INFO";
        case LOG_ERROR: return "ERROR";
        case LOG_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void Logger::dumpToSerial() {
    Serial.println(F("\n========== DEBUG LOG DUMP =========="));
    Serial.print(F("Total entries: "));
    Serial.println(logBuffer.size());
    Serial.println(F("====================================\n"));
    
    for (const auto& entry : logBuffer) {
        Serial.print("[");
        Serial.print(entry.timestamp);
        Serial.print("] ");
        Serial.print(levelToString(entry.level));
        Serial.print(": ");
        Serial.println(entry.message);
    }
    
    Serial.println(F("\n========== END LOG DUMP ==========\n"));
}

void Logger::clearLog() {
    logBuffer.clear();
    
    if (LittleFS.exists(LOG_FILE)) {
        LittleFS.remove(LOG_FILE);
    }
    
    info("Log cleared");
    Serial.println(F("[Logger] Log cleared"));
}

void Logger::checkSerialConnection() {
    // Detect if serial is connected (data ready to read indicates connection)
    bool nowConnected = Serial.available() > 0 || Serial;
    
    if (nowConnected && !serialConnected) {
        // Just connected - auto dump log
        serialConnected = true;
        delay(500);  // Give serial time to stabilize
        dumpToSerial();
    } else if (!nowConnected && serialConnected) {
        // Disconnected
        serialConnected = false;
    }
}

void Logger::update() {
    checkSerialConnection();
    
    // Check for serial commands
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd == "!GETLOG") {
            dumpToSerial();
        } else if (cmd == "!CLEARLOG") {
            clearLog();
        }
    }
}
