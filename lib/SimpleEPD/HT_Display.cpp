// Simplified HT_Display implementation
// Extracted from Heltec_ESP32 library

#include "HT_Display.h"
#include <SPI.h>

// Define the global SPI object once
SPIClass fSPI(FSPI);

// Swap macro needs to be defined before use
#ifndef _swap_int16_t
#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
#endif

// Font data - simplified Arial fonts
#include "HT_DisplayFonts.h"

ScreenDisplay::ScreenDisplay() {
    buffer = nullptr;
    displayWidth = 0;
    displayHeight = 0;
    displayBufferSize = 0;
    color = WHITE;
    textAlignment = TEXT_ALIGN_LEFT;
    fontData = ArialMT_Plain_10;
}

ScreenDisplay::~ScreenDisplay() {
    end();
}

bool ScreenDisplay::init() {
    if (!connect()) {
        return false;
    }
    sendInitCommands();
    return true;
}

void ScreenDisplay::end() {
    if (buffer) {
        // Note: buffer is pointing to _bbf array in derived class, don't free it
    }
}

void ScreenDisplay::setGeometry(DISPLAY_GEOMETRY g, uint16_t width, uint16_t height) {
    geometry = g;
    switch (g) {
        case GEOMETRY_128_64:
            this->displayWidth = 128;
            this->displayHeight = 64;
            break;
        case GEOMETRY_128_32:
            this->displayWidth = 128;
            this->displayHeight = 32;
            break;
        case GEOMETRY_296_128:
            this->displayWidth = 296;
            this->displayHeight = 128;
            break;
        case GEOMETRY_250_122:
            this->displayWidth = 250;
            this->displayHeight = 122;
            break;
        default:
            this->displayWidth = width > 0 ? width : 64;
            this->displayHeight = height > 0 ? height : 48;
            break;
    }
    this->displayBufferSize = (this->displayWidth * this->displayHeight) / 8;
}

void ScreenDisplay::setColor(DISPLAY_COLOR color) {
    this->color = color;
}

DISPLAY_COLOR ScreenDisplay::getColor() {
    return this->color;
}

void ScreenDisplay::setPixel(int16_t x, int16_t y) {
    if (x >= 0 && x < this->displayWidth && y >= 0 && y < this->displayHeight) {
        switch (this->color) {
            case WHITE:
                buffer[x + (y / 8) * this->displayWidth] |= (1 << (y & 7));
                break;
            case BLACK:
                buffer[x + (y / 8) * this->displayWidth] &= ~(1 << (y & 7));
                break;
            case INVERSE:
                buffer[x + (y / 8) * this->displayWidth] ^= (1 << (y & 7));
                break;
        }
    }
}

void ScreenDisplay::clear() {
    memset(buffer, 0, displayBufferSize);
}

void ScreenDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    int16_t steep = abs(y1 - y0) > abs(x1 - x0);
    if (steep) {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }
    if (x0 > x1) {
        _swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
    }
    int16_t dx, dy;
    dx = x1 - x0;
    dy = abs(y1 - y0);
    int16_t err = dx / 2;
    int16_t ystep;
    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }
    for (; x0 <= x1; x0++) {
        if (steep) {
            setPixel(y0, x0);
        } else {
            setPixel(x0, y0);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

void ScreenDisplay::drawRect(int16_t x, int16_t y, int16_t width, int16_t height) {
    drawHorizontalLine(x, y, width);
    drawVerticalLine(x, y, height);
    drawVerticalLine(x + width - 1, y, height);
    drawHorizontalLine(x, y + height - 1, width);
}

void ScreenDisplay::fillRect(int16_t xMove, int16_t yMove, int16_t width, int16_t height) {
    for (int16_t x = xMove; x < xMove + width; x++) {
        drawVerticalLine(x, yMove, height);
    }
}

void ScreenDisplay::drawHorizontalLine(int16_t x, int16_t y, int16_t length) {
    for (int16_t i = x; i < x + length; i++) {
        setPixel(i, y);
    }
}

void ScreenDisplay::drawVerticalLine(int16_t x, int16_t y, int16_t length) {
    for (int16_t i = y; i < y + length; i++) {
        setPixel(x, i);
    }
}

void ScreenDisplay::drawCircle(int16_t x0, int16_t y0, int16_t radius) {
    int16_t x = 0, y = radius;
    int16_t dp = 1 - radius;
    do {
        if (dp < 0)
            dp = dp + 2 * (++x) + 3;
        else
            dp = dp + 2 * (++x) - 2 * (--y) + 5;

        setPixel(x0 + x, y0 + y);
        setPixel(x0 - x, y0 + y);
        setPixel(x0 + x, y0 - y);
        setPixel(x0 - x, y0 - y);
        setPixel(x0 + y, y0 + x);
        setPixel(x0 - y, y0 + x);
        setPixel(x0 + y, y0 - x);
        setPixel(x0 - y, y0 - x);
    } while (x < y);
}

void ScreenDisplay::fillCircle(int16_t x0, int16_t y0, int16_t radius) {
    for (int16_t y = -radius; y <= radius; y++) {
        for (int16_t x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                setPixel(x0 + x, y0 + y);
            }
        }
    }
}

void ScreenDisplay::setFont(const uint8_t *fontData) {
    this->fontData = fontData;
}

void ScreenDisplay::setTextAlignment(DISPLAY_TEXT_ALIGNMENT textAlignment) {
    this->textAlignment = textAlignment;
}

void ScreenDisplay::drawString(int16_t xMove, int16_t yMove, String text) {
    uint16_t textWidth = getStringWidth(text);
    char *textBuffer = new char[text.length() + 1];
    text.toCharArray(textBuffer, text.length() + 1);
    drawStringInternal(xMove, yMove, textBuffer, text.length(), textWidth);
    delete[] textBuffer;
}

void ScreenDisplay::drawStringInternal(int16_t xMove, int16_t yMove, char* text, uint16_t textLength, uint16_t textWidth) {
    // Render text using 5x7 bitmap font
    int16_t cursorX = xMove;
    
    if (textAlignment == TEXT_ALIGN_CENTER) {
        cursorX -= textWidth / 2;
    } else if (textAlignment == TEXT_ALIGN_RIGHT) {
        cursorX -= textWidth;
    }
    
    if (textAlignment == TEXT_ALIGN_CENTER_BOTH) {
        yMove -= 4; // Half of 7-pixel font height
    }
    
    // Simple character rendering
    for (uint16_t i = 0; i < textLength; i++) {
        char c = text[i];
        
        // Only render printable ASCII characters (32-126)
        if (c >= 32 && c <= 126) {
            // Calculate offset into font data (5 bytes per character)
            int charIndex = (c - 32) * 5;
            
            // Draw each column of the character
            for (int col = 0; col < 5; col++) {
                uint8_t columnData = pgm_read_byte(font5x7_data + charIndex + col);
                
                // Draw each pixel in the column
                for (int row = 0; row < 7; row++) {
                    if (columnData & (1 << row)) {
                        setPixel(cursorX + col, yMove + row);
                    }
                }
            }
            
            cursorX += 6; // 5 pixels + 1 pixel spacing
        } else {
            // Space for unprintable characters
            cursorX += 6;
        }
    }
}

uint16_t ScreenDisplay::getStringWidth(String text) {
    // Simplified - return approximate width
    return text.length() * 6;
}

void ScreenDisplay::screenRotate(DISPLAY_ANGLE angle) {
    this->rotate_angle = angle;
}

size_t ScreenDisplay::write(uint8_t c) {
    // Simplified write
    return 1;
}
