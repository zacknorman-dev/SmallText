#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

// Heltec Vision Master E290 battery pins
#define BATTERY_PIN 7
#define ADC_CTRL 46
#define ADC_CTRL_ENABLED HIGH
#define ADC_CHANNEL ADC1_CHANNEL_6  // GPIO7 is ADC1 Channel 6 on ESP32-S3
#define ADC_MULTIPLIER 5.047  // 4.9 * 1.03
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5
#define BATTERY_SENSE_SAMPLES 10

class Battery {
private:
    float lastVoltage;
    int lastPercent;
    unsigned long lastReadTime;
    static const unsigned long READ_INTERVAL = 30000;  // Read every 30 seconds
    
    void enableADC();
    void disableADC();
    float readRawVoltage();
    int voltageToPercent(float voltage);
    
public:
    Battery();
    
    bool begin();
    void update();  // Call periodically to update readings
    
    float getVoltage();  // Returns battery voltage in volts
    int getPercent();    // Returns battery percentage 0-100
    bool isCharging();   // Future: detect if charging
};

#endif
