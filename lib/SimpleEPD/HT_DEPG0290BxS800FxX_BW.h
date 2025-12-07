// Simplified DEPG0290 e-paper display driver
// Extracted from Heltec_ESP32 library

#ifndef __DEPG0290BxS800FxX_BW_H__
#define __DEPG0290BxS800FxX_BW_H__

#include "HT_Display.h"
#include <SPI.h>

extern SPIClass fSPI;

class DEPG0290BxS800FxX_BW : public ScreenDisplay {
private:
    uint8_t _rst;
    uint8_t _dc;
    int8_t _cs;
    int8_t _clk;
    int8_t _mosi;
    int8_t _miso;
    uint32_t _freq;
    int8_t _busy;
    uint8_t _bbf[4736];  // Buffer size for 296x128 display
    SPISettings _spiSettings;

public:
    DEPG0290BxS800FxX_BW(uint8_t rst, uint8_t dc, int8_t cs, int8_t busy, int8_t sck, int8_t mosi, int8_t miso, uint32_t freq = 6000000, DISPLAY_GEOMETRY g = GEOMETRY_296_128) {
        setGeometry(g);
        this->_rst = rst;
        this->_dc = dc;
        this->_cs = cs;
        this->_freq = freq;
        this->_clk = sck;
        this->_mosi = mosi;
        this->_miso = miso;
        this->_busy = busy;
        this->displayType = E_INK;
    }

    bool connect() {
        pinMode(_dc, OUTPUT);
        pinMode(_rst, OUTPUT);
        pinMode(_cs, OUTPUT);
        digitalWrite(_cs, HIGH);
        pinMode(_busy, INPUT);
        this->buffer = _bbf;
        fSPI.begin(this->_clk, this->_miso, this->_mosi);
        _spiSettings._clock = this->_freq;
        
        // Reset sequence
        digitalWrite(_rst, HIGH);
        delay(100);
        digitalWrite(_rst, LOW);
        delay(100);
        digitalWrite(_rst, HIGH);
        return true;
    }

    void display(void) {
        if(rotate_angle == ANGLE_0_DEGREE || rotate_angle == ANGLE_180_DEGREE) {
            sendCommand(0x24);
            digitalWrite(_cs, LOW);
            int xmax = this->width();
            int ymax = this->height() >> 3;
            
            if(rotate_angle == ANGLE_0_DEGREE) {
                fSPI.beginTransaction(SPISettings(6000000, LSBFIRST, SPI_MODE0));
                for(int x = 0; x < xmax; x++) {
                    for(int y = 0; y < ymax; y++) {
                        fSPI.transfer(~buffer[x + y * xmax]);
                    }
                }
            } else {
                fSPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE0));
                for(int x = xmax - 1; x >= 0; x--) {
                    for(int y = (ymax - 1); y >= 0; y--) {
                        fSPI.transfer(~buffer[x + y * xmax]);
                    }
                }
            }
            fSPI.endTransaction();
            digitalWrite(_cs, HIGH);
            sendCommand(0x20);
            WaitUntilIdle();
        } else {
            uint8_t buffer_rotate[displayBufferSize];
            memset(buffer_rotate, 0, displayBufferSize);
            uint8_t temp;
            
            for(uint16_t i = 0; i < this->width(); i++) {
                for(uint16_t j = 0; j < this->height(); j++) {
                    temp = buffer[(j >> 3) * this->width() + i] >> (j & 7) & 0x01;
                    buffer_rotate[(i >> 3) * this->height() + j] |= (temp << (i & 7));
                }
            }
            
            sendCommand(0x24);
            digitalWrite(_cs, LOW);
            int xmax = this->height();
            int ymax = this->width() >> 3;
            
            if(rotate_angle == ANGLE_90_DEGREE) {
                fSPI.beginTransaction(SPISettings(6000000, MSBFIRST, SPI_MODE0));
                for(int x = 0; x < xmax; x++) {
                    for(int y = ymax - 1; y >= 0; y--) {
                        fSPI.transfer(~buffer_rotate[x + y * xmax]);
                    }
                }
            } else {
                fSPI.beginTransaction(SPISettings(6000000, LSBFIRST, SPI_MODE0));
                for(int x = xmax - 1; x >= 0; x--) {
                    for(int y = 0; y < ymax; y++) {
                        fSPI.transfer(~buffer_rotate[x + y * xmax]);
                    }
                }
            }
            fSPI.endTransaction();
            digitalWrite(_cs, HIGH);
            sendCommand(0x20);
            WaitUntilIdle();
        }
    }

    void stop() {
        end();
    }

private:
    int getBufferOffset(void) {
        return 0;
    }

    void WaitUntilIdle() {
        while(digitalRead(_busy)) {  // LOW: idle, HIGH: busy
            delay(1);
        }
        delay(100);
    }

    inline void sendCommand(uint8_t com) __attribute__((always_inline)) {
        digitalWrite(_dc, LOW);
        digitalWrite(_cs, LOW);
        fSPI.beginTransaction(_spiSettings);
        fSPI.transfer(com);
        fSPI.endTransaction();
        digitalWrite(_cs, HIGH);
        digitalWrite(_dc, HIGH);
    }

    void sendData(unsigned char data) {
        digitalWrite(this->_cs, LOW);
        fSPI.transfer(data);
        digitalWrite(this->_cs, HIGH);
    }

    void sendInitCommands(void) {
        if (geometry == GEOMETRY_RAWMODE)
            return;
        WaitUntilIdle();
        sendCommand(0x12);  // soft reset
        WaitUntilIdle();
    }

    void sendScreenRotateCommand() {}
};

#endif
