# GB Printer emulator

GB Printer emulator based on ESP32.

## Configuration and build

Configuration used for development:

- `FireBeetle 2 ESP32-E IoT`
- `ESP-IDF v5.3-dev-3499-g636ff35b52`
- `Linux Mint 21.3`.

```bash
idf.py build
```

### Pinout

Wire color may vary.

- pin 4 - Tx - yellow
- pin 12 - Rx - green
- pin 17 - clock - orange
- ground - black

#### WARNING

Logic level converter might be required.
GB use 5V voltage, while ESP32 works with 3.3V.
I take no responsibility for fried devices.

My personal GB Pocket outputs 3.5V and I didn't encounter any issues without logic level converter.

## Usage

Use EspTouch to configure Wi-Fi.
After it is configured - access device using `http://gb-printer/`.
