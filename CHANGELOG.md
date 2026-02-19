# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial firmware release
- OLED display support (SSD1306 128x64)
- Button input with event reporting
- Buzzer output (tones and melodies)
- WebSocket connection to OpenClaw Gateway
- System info command
- Heartbeat keepalive
- GitHub Actions CI/CD workflows
- Contributing guidelines

### Hardware Support
- Seeed XIAO ESP32-C3
- XIAO Expansion Board

## [1.0.0] - 2026-02-19

### Added
- `canvas.show` - Display text on OLED
- `canvas.clear` - Clear OLED display
- `audio.beep` - Play single tone
- `audio.tone` - Play melody sequence
- `system.info` - Report device information
- `input.button` - Button press/release events
- Automatic WiFi reconnection
- WebSocket reconnection with backoff
- Status bar on OLED showing WiFi/Gateway state

### Technical
- PlatformIO build system
- ArduinoJson 7.x for JSON handling
- U8g2 graphics library for OLED
- WebSockets library for gateway communication

---

## Version History

| Version | Date | Notes |
|---------|------|-------|
| 1.0.0 | 2026-02-19 | Initial release |

[Unreleased]: https://github.com/chilu18/openclaw-esp32c3-xiao-node/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/chilu18/openclaw-esp32c3-xiao-node/releases/tag/v1.0.0
