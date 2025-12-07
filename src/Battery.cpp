#include "Battery.h"
#include <driver/adc.h>
#include <esp_adc_cal.h>

Battery::Battery() {
    lastVoltage = 0.0;
    lastPercent = 0;
    lastReadTime = 0;
}

bool Battery::begin() {
    // Configure ADC control pin
    pinMode(ADC_CTRL, OUTPUT);
    disableADC();  // Start with ADC disabled to save power
    
    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTENUATION);
    
    Serial.println("[Battery] Initialized");
    
    // Take initial reading
    update();
    
    return true;
}

void Battery::enableADC() {
    digitalWrite(ADC_CTRL, ADC_CTRL_ENABLED);
    delay(10);  // Let voltage divider stabilize
}

void Battery::disableADC() {
    digitalWrite(ADC_CTRL, !ADC_CTRL_ENABLED);
}

float Battery::readRawVoltage() {
    enableADC();
    
    uint32_t raw = 0;
    int validSamples = 0;
    
    // Take multiple samples and average
    for (int i = 0; i < BATTERY_SENSE_SAMPLES; i++) {
        int reading = adc1_get_raw(ADC_CHANNEL);
        if (reading >= 0) {
            raw += reading;
            validSamples++;
        }
        delay(1);
    }
    
    disableADC();
    
    if (validSamples == 0) {
        Serial.println("[Battery] No valid ADC readings");
        return 0.0;
    }
    
    // Calculate average
    raw = raw / validSamples;
    
    // Convert to voltage (12-bit ADC, 2.5dB attenuation range is ~0-1250mV)
    // Reference voltage for ESP32-S3 with 2.5dB attenuation
    float voltage = (raw / 4095.0) * 1.25;  // ADC reading to reference voltage
    voltage *= ADC_MULTIPLIER;  // Apply voltage divider multiplier
    
    return voltage;
}

int Battery::voltageToPercent(float voltage) {
    // LiPo voltage curve (approximate)
    // 4.2V = 100%, 3.7V = 50%, 3.3V = 0%
    
    if (voltage >= 4.2) return 100;
    if (voltage <= 3.3) return 0;
    
    // Linear approximation (can be improved with proper discharge curve)
    if (voltage >= 3.7) {
        // 3.7V to 4.2V = 50% to 100%
        return 50 + (int)((voltage - 3.7) / 0.5 * 50);
    } else {
        // 3.3V to 3.7V = 0% to 50%
        return (int)((voltage - 3.3) / 0.4 * 50);
    }
}

void Battery::update() {
    // Rate limit readings to save power
    if (millis() - lastReadTime < READ_INTERVAL && lastReadTime != 0) {
        return;
    }
    
    lastReadTime = millis();
    lastVoltage = readRawVoltage();
    lastPercent = voltageToPercent(lastVoltage);
    
    Serial.printf("[Battery] Voltage: %.2fV, Percent: %d%%\n", lastVoltage, lastPercent);
}

float Battery::getVoltage() {
    return lastVoltage;
}

int Battery::getPercent() {
    return lastPercent;
}

bool Battery::isCharging() {
    // Future: implement charging detection if hardware supports it
    return false;
}
