/*
 * Meshtastic Active Probe Scanner for Heltec Vision Master E290
 * 
 * Actively probes for Meshtastic nodes by sending broadcast pings
 * - Sends NODEINFO request every 30 seconds on each channel
 * - Flashes LED when responses received
 * - Shows discovered nodes on e-ink display
 * - Cycles through channels every 60 seconds
 * 
 * Much faster than passive listening - finds idle nodes that aren't chatting
 */

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_290_BS.h>
#include <Fonts/FreeSans9pt7b.h>
#include <vector>
#include <set>

// Heltec Vision Master E290 pins
#define RADIO_SCLK_PIN       9
#define RADIO_MISO_PIN      11
#define RADIO_MOSI_PIN      10
#define RADIO_CS_PIN         8
#define RADIO_DIO1_PIN      14
#define RADIO_RST_PIN       12
#define RADIO_BUSY_PIN      13

// LED pin (Heltec boards typically use GPIO 35 for onboard LED)
#define LED_PIN 35

// Display pins (same as main app)
#define EPD_RST 5
#define EPD_DC 4
#define EPD_CS 3
#define EPD_BUSY 6
#define EPD_SCK 2
#define EPD_MOSI 1

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
SPIClass* displaySPI = nullptr;
GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>* display = nullptr;

// Meshtastic channel definitions (US 915MHz)
struct MeshtasticChannel {
  const char* name;
  float freq;      // MHz
  float bw;        // kHz
  int sf;          // Spreading factor
  int cr;          // Coding rate
};

MeshtasticChannel channels[] = {
  {"LongFast",    906.875, 250.0, 11, 8},  // Channel 0 - Most common public
  {"LongSlow",    906.875, 125.0, 11, 8},  // Channel 1
  {"VeryLong",    906.875,  62.5, 12, 8},  // Channel 2
  {"MediumFast",  906.875, 250.0,  9, 8},  // Channel 3
  {"MediumSlow",  906.875, 125.0,  9, 8},  // Channel 4
  {"ShortFast",   906.875, 250.0,  7, 8},  // Channel 5
  {"ShortSlow",   906.875, 125.0,  7, 8},  // Channel 6
};

int numChannels = 7;
int currentChannel = 0;
unsigned long channelStartTime = 0;
unsigned long channelDwell = 60000; // 60 seconds per channel
unsigned long lastProbeTime = 0;
unsigned long probeInterval = 30000; // Send probe every 30 seconds

int totalPackets = 0;
int packetsThisChannel = 0;
unsigned long lastPacketTime = 0;
bool needsDisplayUpdate = false;

struct DetectedNode {
  uint32_t nodeId;
  int rssi;
  int channelIndex;
  unsigned long lastSeen;
};

std::vector<DetectedNode> detectedNodes;
std::set<uint32_t> seenNodeIds;

// Generate a random node ID for our scanner
uint32_t myNodeId = 0;

// Forward declarations
void flashLED();
void updateDisplay();
void switchChannel(int channel);

// Simple Meshtastic packet structure (simplified - not full protobuf)
// Just enough to send a broadcast ping and parse basic responses
void sendProbe() {
  // Meshtastic minimal broadcast packet
  // This is a simplified NODEINFO_APP request
  // Real Meshtastic uses protobuf, but we can send a recognizable packet
  
  uint8_t packet[32];
  memset(packet, 0, sizeof(packet));
  
  // Byte 0: Packet header (0x00 = broadcast)
  packet[0] = 0x00;
  
  // Bytes 1-4: From (our node ID, big-endian)
  packet[1] = (myNodeId >> 24) & 0xFF;
  packet[2] = (myNodeId >> 16) & 0xFF;
  packet[3] = (myNodeId >> 8) & 0xFF;
  packet[4] = myNodeId & 0xFF;
  
  // Bytes 5-8: To (0xFFFFFFFF = broadcast)
  packet[5] = 0xFF;
  packet[6] = 0xFF;
  packet[7] = 0xFF;
  packet[8] = 0xFF;
  
  // Bytes 9-12: Packet ID (random)
  uint32_t packetId = random(0xFFFFFFFF);
  packet[9] = (packetId >> 24) & 0xFF;
  packet[10] = (packetId >> 16) & 0xFF;
  packet[11] = (packetId >> 8) & 0xFF;
  packet[12] = packetId & 0xFF;
  
  // Byte 13: Port number (3 = NODEINFO_APP in Meshtastic)
  packet[13] = 0x03;
  
  // Byte 14: Flags (want reply)
  packet[14] = 0x01;
  
  // Rest is padding
  
  Serial.print("[PROBE] Sending on ");
  Serial.print(channels[currentChannel].name);
  Serial.print("...");
  
  int state = radio.transmit(packet, 32);
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(" Sent!");
    flashLED();
    flashLED(); // Double flash for transmit
  } else {
    Serial.print(" Failed: ");
    Serial.println(state);
  }
  
  // Return to RX mode
  radio.startReceive();
  lastProbeTime = millis();
}

void flashLED() {
  digitalWrite(LED_PIN, HIGH);
  delay(50);
  digitalWrite(LED_PIN, LOW);
}

void updateDisplay() {
  if (!display) return;
  
  display->setFullWindow();
  display->firstPage();
  do {
    display->fillScreen(GxEPD_WHITE);
    display->setTextColor(GxEPD_BLACK);
    
    // Title
    display->setFont(&FreeSans9pt7b);
    display->setCursor(10, 20);
    display->print("MESHTASTIC PROBE");
    
    display->setFont();
    
    // Show discovered nodes if any
    if (detectedNodes.size() > 0) {
      display->setCursor(10, 40);
      display->print("NODES FOUND: ");
      display->print(detectedNodes.size());
      
      display->setCursor(10, 60);
      display->print("--- DETECTED ---");
      
      int y = 75;
      for (size_t i = 0; i < min((size_t)3, detectedNodes.size()); i++) {
        display->setCursor(10, y);
        display->print("0x");
        display->print(detectedNodes[i].nodeId, HEX);
        display->setCursor(150, y);
        display->print(detectedNodes[i].rssi);
        display->print("dBm");
        y += 15;
      }
      
      if (detectedNodes.size() > 3) {
        display->setCursor(10, y);
        display->print("+ ");
        display->print(detectedNodes.size() - 3);
        display->print(" more");
      }
    } else {
      // Just show scanning message
      display->setCursor(60, 60);
      display->print("Scanning...");
      
      display->setCursor(40, 80);
      display->print("Channel: ");
      display->print(channels[currentChannel].name);
    }
    
  } while (display->nextPage());
  
  needsDisplayUpdate = false;
}

void switchChannel(int channel) {
  currentChannel = channel;
  packetsThisChannel = 0;
  
  MeshtasticChannel& ch = channels[currentChannel];
  
  Serial.print("\n[SCAN] Switching to channel: ");
  Serial.print(ch.name);
  Serial.print(" (");
  Serial.print(ch.freq, 3);
  Serial.println(" MHz)");
  
  int state = radio.begin(
    ch.freq,
    ch.bw,
    ch.sf,
    ch.cr,
    0x2B,    // Meshtastic sync word
    22,
    8,
    0,
    false
  );
  
  if (state == RADIOLIB_ERR_NONE) {
    radio.startReceive();
    channelStartTime = millis();
    lastProbeTime = 0; // Trigger immediate probe on new channel
    updateDisplay();
  } else {
    Serial.print("[ERROR] Failed to switch channel: ");
    Serial.println(state);
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Enable Vext power for peripherals (CRITICAL!)
  pinMode(18, OUTPUT);
  digitalWrite(18, HIGH);
  delay(100);
  
  // Generate random node ID for this scanner
  randomSeed(analogRead(0));
  myNodeId = random(0x10000000, 0xFFFFFFFF);
  
  // Initialize LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Flash LED to show startup
  for (int i = 0; i < 3; i++) {
    flashLED();
    delay(100);
  }
  
  Serial.println("\n\n=== MESHTASTIC ACTIVE PROBE ===");
  Serial.print("Scanner Node ID: 0x");
  Serial.println(myNodeId, HEX);
  Serial.println("Sending broadcast probes to discover nodes...\n");
  Serial.println("Watch for LED flashes and check serial output for results.\n");

  // Initialize SPI for radio FIRST (default SPI = VSPI)
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  
  Serial.print("[Radio] Initializing... ");
  Serial.flush();
  
  // Initialize radio on first channel
  int state = radio.begin(
    906.875,
    250.0,
    11,
    8,
    0x2B,
    22,
    8,
    0,
    false
  );
  
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("SUCCESS!");
    radio.startReceive();
  } else {
    Serial.print("FAILED! Error: ");
    Serial.println(state);
  }
  Serial.flush();
  delay(100);

  // NOW initialize display on separate SPI bus (HSPI) - AFTER radio
  Serial.println("[Display] Initializing e-ink...");
  Serial.flush();
  
  displaySPI = new SPIClass(HSPI);
  displaySPI->begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  
  display = new GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>(GxEPD2_290_BS(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
  display->epd2.selectSPI(*displaySPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display->init(115200, false, 2, false);
  display->setRotation(1);
  display->setTextColor(GxEPD_BLACK);
  
  Serial.println("[Display] Success!");
  
  // Show startup screen
  display->setFullWindow();
  display->firstPage();
  do {
    display->fillScreen(GxEPD_WHITE);
    display->setFont(&FreeSans9pt7b);
    display->setCursor(10, 60);
    display->print("MESHTASTIC PROBE");
    display->setFont();
    display->setCursor(60, 80);
    display->print("Scanning...");
  } while (display->nextPage());
  
  delay(500);
  
  // Set initial channel tracking
  currentChannel = 0;
  channelStartTime = millis();
  lastProbeTime = 0;
  
  Serial.println("Ready! Will probe every 30 seconds.\n");
}

void loop() {
  // Send probe if it's time
  if (millis() - lastProbeTime > probeInterval) {
    sendProbe();
  }
  
  // Check for received packet
  if (radio.getPacketLength() > 0) {
    byte packet[256];
    int state = radio.readData(packet, 256);
    
    if (state == RADIOLIB_ERR_NONE) {
      totalPackets++;
      packetsThisChannel++;
      lastPacketTime = millis();
      
      // Get signal stats
      float rssi = radio.getRSSI();
      float snr = radio.getSNR();
      int len = radio.getPacketLength();
      
      // Try to extract node ID from packet (bytes 1-4, big-endian)
      uint32_t nodeId = 0;
      if (len >= 8) {
        nodeId = ((uint32_t)packet[1] << 24) | 
                 ((uint32_t)packet[2] << 16) | 
                 ((uint32_t)packet[3] << 8) | 
                 ((uint32_t)packet[4]);
      }
      
      // If we got a valid node ID and it's not our own probe
      if (nodeId != 0 && nodeId != myNodeId && nodeId != 0xFFFFFFFF) {
        // Check if this is a new node
        bool isNew = (seenNodeIds.find(nodeId) == seenNodeIds.end());
        
        if (isNew) {
          seenNodeIds.insert(nodeId);
          
          DetectedNode node;
          node.nodeId = nodeId;
          node.rssi = (int)rssi;
          node.channelIndex = currentChannel;
          node.lastSeen = millis();
          detectedNodes.push_back(node);
          
          Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
          Serial.println("🎯 NEW NODE DISCOVERED!");
          Serial.print("   Node ID: 0x");
          Serial.println(nodeId, HEX);
          Serial.print("   Channel: ");
          Serial.println(channels[currentChannel].name);
          Serial.print("   RSSI: ");
          Serial.print(rssi, 1);
          Serial.print(" dBm | SNR: ");
          Serial.print(snr, 1);
          Serial.println(" dB");
          Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
          
          // Trigger display update when new node found
          needsDisplayUpdate = true;
          updateDisplay();
        }
        
        // Flash LED for any response
        flashLED();
      }
      
      // Print packet info
      Serial.print("[RX] Ch:");
      Serial.print(channels[currentChannel].name);
      Serial.print(" RSSI:");
      Serial.print(rssi, 1);
      Serial.print(" NodeID:0x");
      Serial.println(nodeId, HEX);
    }
    
    // Continue receiving
    radio.startReceive();
  }
  
  // Check if it's time to switch channels
  if (millis() - channelStartTime > channelDwell) {
    int nextChannel = (currentChannel + 1) % numChannels;
    
    // Print summary before switching
    if (detectedNodes.size() > 0) {
      Serial.println("\n╔══════════════════════════════════════════╗");
      Serial.println("║       MESHTASTIC NODES DISCOVERED        ║");
      Serial.println("╚══════════════════════════════════════════╝");
      for (const auto& node : detectedNodes) {
        Serial.print("Node 0x");
        Serial.print(node.nodeId, HEX);
        Serial.print(" on ");
        Serial.print(channels[node.channelIndex].name);
        Serial.print(" (");
        Serial.print(node.rssi);
        Serial.println(" dBm)");
      }
      Serial.println("\n✅ Meshtastic infrastructure EXISTS!");
      Serial.println("   You can piggyback on these nodes.\n");
    }
    
    switchChannel(nextChannel);
  }
  
  delay(10);
}
