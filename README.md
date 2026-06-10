# Waage - Smart Drinking Scale

An ESP32-based drinking scale that challenges you to drink a specific amount from your glass with precision.

## Hardware

- **Board**: ESP32-C3 Super Mini
- **Display**: SSD1306 OLED (128x32, I2C)
- **Load Cell**: HX711 module
- **Button**: Reset button on GPIO 0

### Pin Configuration

- OLED SDA: GPIO 8
- OLED SCL: GPIO 9
- HX711 DAT: GPIO 21
- HX711 CLK: GPIO 20
- Button: GPIO 5

## Features

- **Weight Measurement**: Precise measurement using HX711 load cell amplifier
- **Drinking Challenge**: Set a goal weight and try to drink exactly that amount
- **OLED Display**: Real-time feedback and animations
- **Web Configuration**: Configure scale calibration and goals via WiFi
- **State Machine**: Idle → Tare → Drinking → Result flow

## States

1. **Idle**: Waiting for a glass with sufficient weight (≥ goal)
2. **Tare**: Ready state with motivational drinking phrase
3. **Drinking**: Animated loading screen while drinking
4. **Result**: Shows how much you drank with feedback

## Configuration

Access the web configuration interface via WiFi to set:

- Scale calibration factor
- Tare offset
- Drinking goal (grams)
- Tolerance (grams)
- AP SSID

Configuration is stored in EEPROM and loaded on startup.

## Building & Uploading

Using Arduino CLI with the sketch.yaml configuration:

```bash
arduino-cli compile
arduino-cli upload
```

Default FQBN: `espressif:esp:nologo_esp32c3_super_mini`

> **Multiplayer note:** The duel protocol carries a magic/version byte
> (`DUELL_MAGIC` in `duell.cpp`). Scales with different protocol versions
> ignore each other, so flash **all** scales together when updating.

## Dependencies

- Adafruit_SSD1306
- HX711
- WiFi (ESP32)
- Wire (I2C)

## Usage

1. Place your full glass on the scale (weight ≥ goal)
2. Wait for "Bereit?" message and drinking phrase
3. Lift the glass and drink
4. Place the glass back on the scale
5. View your result with performance feedback

## License

MIT
