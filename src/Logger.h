#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

// Log levels
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_ERROR = 2,
    LOG_CRITICAL = 3
};

class Logger {
private:
    static const char* LOG_FILE;
    static const size_t MAX_LOG_SIZE = 50000;  // 50KB max
    static const size_t MAX_ENTRIES = 500;     // ~500 log entries
    static const size_t PRUNE_COUNT = 125;     // Delete 25% when full
    
    LogLevel currentLevel;
    unsigned long bootTime;
    bool serialConnected;
    
    struct LogEntry {
        unsigned long timestamp;
        LogLevel level;
        String message;
    };
    
    std::vector<LogEntry> logBuffer;
    
    void pruneOldEntries();
    void writeBufferToFile();
    void loadBufferFromFile();
    String levelToString(LogLevel level);
    void checkSerialConnection();
    
public:
    Logger();
    
    bool begin();
    void setLogLevel(LogLevel level);
    
    // Main logging functions
    void log(LogLevel level, const String& message);
    void debug(const String& message);
    void info(const String& message);
    void error(const String& message);
    void critical(const String& message);
    
    // Utility functions
    void logBoot();
    void logSessionMarker(const String& marker);
    
    // Dump log to serial
    void dumpToSerial();
    void clearLog();
    
    // Call periodically to check for serial connection and auto-dump
    void update();
};

// Global logger instance
extern Logger logger;

#endif
