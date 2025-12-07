// Simplified HT_Display base class for DEPG0290 e-paper
// Extracted from Heltec_ESP32 library

#ifndef DISPLAY_h
#define DISPLAY_h

#include <Arduino.h>

enum DISPLAY_COLOR {
  BLACK = 0,
  WHITE = 1,
  INVERSE = 2
};

enum DISPLAY_TYPE {
  OLED = 0,
  E_INK = 1
};

enum DISPLAY_TEXT_ALIGNMENT {
  TEXT_ALIGN_LEFT = 0,
  TEXT_ALIGN_RIGHT = 1,
  TEXT_ALIGN_CENTER = 2,
  TEXT_ALIGN_CENTER_BOTH = 3
};

enum DISPLAY_GEOMETRY {
  GEOMETRY_128_64 = 0,
  GEOMETRY_128_32,
  GEOMETRY_200_200,
  GEOMETRY_250_122,
  GEOMETRY_296_128,
  GEOMETRY_RAWMODE,
  GEOMETRY_64_32,
};

enum DISPLAY_ANGLE {
  ANGLE_0_DEGREE = 0,
  ANGLE_90_DEGREE,
  ANGLE_180_DEGREE,
  ANGLE_270_DEGREE,
};

// Minimal font structure
extern const uint8_t ArialMT_Plain_10[];
extern const uint8_t ArialMT_Plain_16[];
extern const uint8_t ArialMT_Plain_24[];

class ScreenDisplay : public Print {
public:
    ScreenDisplay();
    virtual ~ScreenDisplay();

    uint16_t width(void) const { return displayWidth; }
    uint16_t height(void) const { return displayHeight; }

    bool init();
    void end();
    
    void setColor(DISPLAY_COLOR color);
    DISPLAY_COLOR getColor();
    
    void setPixel(int16_t x, int16_t y);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void drawRect(int16_t x, int16_t y, int16_t width, int16_t height);
    void fillRect(int16_t x, int16_t y, int16_t width, int16_t height);
    void drawCircle(int16_t x, int16_t y, int16_t radius);
    void fillCircle(int16_t x, int16_t y, int16_t radius);
    void drawHorizontalLine(int16_t x, int16_t y, int16_t length);
    void drawVerticalLine(int16_t x, int16_t y, int16_t length);
    
    void drawString(int16_t x, int16_t y, String text);
    uint16_t getStringWidth(String text);
    void setTextAlignment(DISPLAY_TEXT_ALIGNMENT textAlignment);
    void setFont(const uint8_t *fontData);
    
    void screenRotate(DISPLAY_ANGLE angle);
    virtual void display(void) = 0;
    void clear(void);
    
    size_t write(uint8_t c);
    
    uint8_t *buffer;

protected:
    DISPLAY_GEOMETRY geometry;
    uint16_t displayWidth;
    uint16_t displayHeight;
    uint16_t displayBufferSize;
    DISPLAY_ANGLE rotate_angle = ANGLE_0_DEGREE;
    
    void setGeometry(DISPLAY_GEOMETRY g, uint16_t width = 0, uint16_t height = 0);
    
    DISPLAY_TEXT_ALIGNMENT textAlignment;
    DISPLAY_COLOR color;
    DISPLAY_TYPE displayType;
    const uint8_t *fontData;
    
    virtual int getBufferOffset(void) = 0;
    virtual void sendCommand(uint8_t com) { (void)com; }
    virtual void sendInitCommands() {}
    virtual void sendScreenRotateCommand() {}
    virtual bool connect() { return false; }
    
    void drawStringInternal(int16_t xMove, int16_t yMove, char* text, uint16_t textLength, uint16_t textWidth);
};

#endif
