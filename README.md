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

## TODO list

- enable SmartConfig on button press
- handle image parameters (number of sheets, margins, palette, exposure)
- improve printer error handling (error persistence)
- allow image overwrite