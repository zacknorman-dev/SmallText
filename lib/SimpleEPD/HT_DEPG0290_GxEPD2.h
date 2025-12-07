// GxEPD2 wrapper for DEPG0290BS with partial refresh support
// Implements SimpleEPD interface using GxEPD2_290_BS backend

#ifndef __HT_DEPG0290_GXEPD2_H__
#define __HT_DEPG0290_GXEPD2_H__

#include "HT_Display.h"
#include <GxEPD2_BW.h>
#include <epd/GxEPD2_290_BS.h>

extern SPIClass fSPI;

class DEPG0290_GxEPD2 : public ScreenDisplay {
private:
    uint8_t _rst;
    uint8_t _dc;
    int8_t _cs;
    int8_t _busy;
    GxEPD2_290_BS* _epd;
    GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>* _display;
    bool _partialMode;

public:
    DEPG0290_GxEPD2(uint8_t rst, uint8_t dc, int8_t cs, int8_t busy, int8_t sck, int8_t mosi, int8_t miso, uint32_t freq = 6000000, DISPLAY_GEOMETRY g = GEOMETRY_296_128) {
        setGeometry(g);
        this->_rst = rst;
        this->_dc = dc;
        this->_cs = cs;
        this->_busy = busy;
        this->displayType = E_INK;
        this->_epd = nullptr;
        this->_display = nullptr;
        this->_partialMode = false;
    }

    ~DEPG0290_GxEPD2() {
        if (_display) delete _display;
        if (_epd) delete _epd;
    }

    bool connect() {
        fSPI.begin();
        
        // Create GxEPD2 display instance
        _epd = new GxEPD2_290_BS(_cs, _dc, _rst, _busy);
        _display = new GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>(*_epd);
        
        // Initialize display
        _display->init(115200, true, 2, false);
        
        // Set rotation (180 degrees for upside-down mounting)
        if (rotate_angle == ANGLE_180_DEGREE) {
            _display->setRotation(2);
        } else {
            _display->setRotation(0);
        }
        
        // Allocate internal buffer for SimpleEPD compatibility
        this->buffer = (uint8_t*)malloc(this->width() * ((this->height() + 7) / 8));
        if (!this->buffer) return false;
        
        return true;
    }

    void display(void) {
        // Full refresh - convert buffer and display
        _display->setFullWindow();
        _display->firstPage();
        do {
            // Draw from our buffer
            drawBufferToGxEPD2();
        } while (_display->nextPage());
        _partialMode = false;
    }

    void displayPartial(void) {
        // Partial refresh - only update changed content
        if (!_partialMode) {
            // First partial update - set up partial window
            _display->setPartialWindow(0, 0, this->width(), this->height());
            _partialMode = true;
        }
        
        _display->firstPage();
        do {
            drawBufferToGxEPD2();
        } while (_display->nextPage());
    }

    void clear(void) {
        memset(buffer, 0, this->width() * ((this->height() + 7) / 8));
    }

protected:
    int getBufferOffset(void) override {
        return 0;
    }

private:
    void drawBufferToGxEPD2() {
        // Convert our monochrome buffer to GxEPD2 format
        // Our buffer: 1=white, 0=black (inverted in display())
        // GxEPD2: drawPixel with GxEPD_BLACK or GxEPD_WHITE
        
        int xmax = this->width();
        int ymax = this->height();
        
        for (int y = 0; y < ymax; y++) {
            for (int x = 0; x < xmax; x++) {
                int byte_index = x + (y / 8) * xmax;
                int bit_index = y % 8;
                
                // Read bit from buffer
                bool pixel = (buffer[byte_index] & (1 << bit_index)) != 0;
                
                // Draw to GxEPD2 (invert because our buffer is inverted)
                _display->drawPixel(x, y, pixel ? GxEPD_WHITE : GxEPD_BLACK);
            }
        }
    }
};

#endif // __HT_DEPG0290_GXEPD2_H__
