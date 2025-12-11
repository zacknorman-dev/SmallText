# SmolTxt Releases

This folder contains compiled firmware binaries for each release.

## Installation

1. Download the appropriate `firmware-vX.X.X.bin` file
2. Use esptool.py or PlatformIO to flash:
   ```
   esptool.py --chip esp32s3 --port COM13 write_flash 0x10000 firmware-vX.X.X.bin
   ```

## Release Files

Each release includes:
- `firmware-vX.X.X.bin` - Compiled firmware binary for ESP32-S3
