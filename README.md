# OpenClaw ESP32-C3 XIAO Node

A lightweight OpenClaw node firmware for the **Seeed Studio XIAO ESP32-C3** with the **Expansion Board**.

Turn a $12 microcontroller into a fully-functional OpenClaw peripheral node with display, button input, and audio feedback.

![XIAO ESP32-C3](https://files.seeedstudio.com/wiki/XIAO_WiFi/board-pic.png)

## Features

- 📺 **OLED Display** (128x64) — Show text, status, QR codes
- 🔘 **Button Input** — Physical button events sent to gateway
- 🔊 **Buzzer Output** — Audio feedback, tones, melodies
- 📡 **WiFi Connected** — WebSocket connection to OpenClaw Gateway
- ⏰ **RTC Support** — Real-time clock with battery backup (coming soon)
- 💾 **SD Card** — Local storage for logs (coming soon)

## Hardware Required

| Component | Price | Link |
|-----------|-------|------|
| Seeed XIAO ESP32-C3 | ~$5 | [Seeed Studio](https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html) |
| XIAO Expansion Board | ~$8 | [Seeed Studio](https://www.seeedstudio.com/Seeeduino-XIAO-Expansion-board-p-4746.html) |

**Total: ~$13**

## Pin Mapping

| Function | Pin | GPIO | Notes |
|----------|-----|------|-------|
| OLED SDA | D4 | GPIO6 | I2C Data |
| OLED SCL | D5 | GPIO7 | I2C Clock |
| Button | D1 | GPIO3 | Active LOW, pull-up |
| Buzzer | D3 | GPIO5 | PWM output |
| SD Card CS | D2 | GPIO4 | SPI chip select |
| ADC Input | A0 | GPIO2 | Analog sensor input |

## Quick Start

### 1. Install PlatformIO

```bash
pip install platformio
```

### 2. Clone and Configure

```bash
git clone https://github.com/petermm/openclaw-esp32c3-xiao-node.git
cd openclaw-esp32c3-xiao-node
cp src/config.h.example src/config.h
```

Edit `src/config.h` with your settings:

```cpp
#define WIFI_SSID "YourWiFiNetwork"
#define WIFI_PASSWORD "YourWiFiPassword"
#define GATEWAY_HOST "192.168.1.100"  // Your OpenClaw Gateway IP
#define GATEWAY_PORT 18789
```

### 3. Build and Flash

```bash
# Connect XIAO ESP32-C3 via USB
pio run --target upload

# Monitor serial output
pio device monitor
```

### 4. Pair with Gateway

On your OpenClaw gateway:

```bash
openclaw nodes pending
openclaw nodes approve <request-id>
openclaw nodes list
```

## Node Commands

Once paired, the gateway can invoke these commands:

### Display

```bash
# Show text on OLED
openclaw nodes invoke --node xiao --command canvas.show --params '{"text":"Hello!"}'

# Clear display
openclaw nodes invoke --node xiao --command canvas.clear
```

### Audio

```bash
# Simple beep
openclaw nodes invoke --node xiao --command audio.beep --params '{"frequency":1000,"duration":200}'

# Play melody
openclaw nodes invoke --node xiao --command audio.tone --params '{"melody":[{"f":262,"d":200},{"f":330,"d":200},{"f":392,"d":400}]}'
```

### System

```bash
# Get device info
openclaw nodes invoke --node xiao --command system.info
```

## Events

The node sends events to the gateway:

- **Button Press/Release**: `{"event":"input.button","state":"pressed"|"released"}`
- **Heartbeat**: Periodic status with uptime, RSSI, free heap

## Use Cases

### Payment Terminal
Display transaction amounts, show QR codes for mobile payment, beep on success.

### Kitchen Order Display  
Show incoming orders, button to mark complete, audio alerts for new orders.

### IoT Sensor Hub
Read sensors via Grove connectors, display readings, report to gateway.

### Status Badge
Wearable device showing system status, alerts, notifications.

## Architecture

```
┌─────────────────────┐     WiFi/WebSocket     ┌──────────────────┐
│  ESP32-C3 Node      │ ◄────────────────────► │ OpenClaw Gateway │
│                     │                         │                  │
│  • OLED Display     │     JSON-RPC Messages   │  • AI Model      │
│  • Button           │                         │  • Sessions      │
│  • Buzzer           │                         │  • Channels      │
│  • Sensors          │                         │                  │
└─────────────────────┘                         └──────────────────┘
```

## Development

### Project Structure

```
openclaw-esp32c3-xiao-node/
├── src/
│   ├── main.cpp          # Main firmware
│   ├── config.h          # Your config (gitignored)
│   └── config.h.example  # Template config
├── platformio.ini        # PlatformIO configuration
├── README.md
└── LICENSE
```

### Dependencies

- [U8g2](https://github.com/olikraus/u8g2) — OLED display driver
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson) — JSON parsing
- [WebSockets](https://github.com/Links2004/arduinoWebSockets) — WebSocket client

## Contributing

1. Fork the repo
2. Create a feature branch
3. Make your changes
4. Submit a PR

This project is designed to be upstreamed to the main [OpenClaw](https://github.com/openclaw/openclaw) repository.

## License

MIT License — See [LICENSE](LICENSE)

## Credits

- [OpenClaw](https://github.com/openclaw/openclaw) — The AI assistant framework
- [Seeed Studio](https://www.seeedstudio.com/) — XIAO hardware
- [HeySalad](https://heysalad.com) — Initial development

---

Built with ✨ by Julia & Peter @ HeySalad
