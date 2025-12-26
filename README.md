# Rocket Telemetry System (ESP32 + LoRa)

Flight-computer + ground-station telemetry system for model rockets. Streams live sensor data over LoRa and logs to microSD for post-flight analysis.

## Features
- Live telemetry downlink (LoRa)
- Onboard microSD logging
- Modular sensors (barometer, IMU, GPS)
- Packetized protocol (easy to extend)
- Ground station Serial output (works with laptop dashboards)

## System Overview
**Flight Computer**
- ESP32 + sensors + LoRa + microSD
- Collects telemetry and transmits packets at a fixed rate
- Logs data to SD for recovery if radio drops

**Ground Station**
- LoRa receiver (ESP32/Arduino)
- Prints decoded telemetry over Serial (can be graphed in a dashboard)

## Repository Layout
- `firmware/flight-computer/` – onboard code
- `firmware/ground-station/` – receiver code
- `images/` – build photos + test setup images

## Hardware (typical)
Flight:
- ESP32
- LoRa module (SX127x)
- Barometer (BME/BMP280)
- IMU (MPU6050 or similar)
- GPS (NEO-6M or similar)
- microSD module
- Battery + regulator

Ground:
- ESP32 (or Arduino)
- LoRa module (SX127x)
- Optional OLED/LCD

Pinouts and wiring notes are documented in the code.

## Getting Started
1. Flash `firmware/flight-computer/` to the onboard ESP32 (Arduino IDE or PlatformIO).
2. Flash `firmware/ground-station/` to the receiver.
3. Set matching radio parameters (frequency, spreading factor, bandwidth).
4. Open Serial Monitor on the ground station and confirm telemetry packets appear.

## Project Status
Work in progress. Current focus:
- Stabilize packet decoding + error checking
- Improve logging format + timestamps
- Range tests and flight validation
- Improve vertical accuracy through method testing

## Photos / Build Log
See: `images/`

## License
MIT (add a `LICENSE` file if you want this to be official)
