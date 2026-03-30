# OpenClaw ESP32-C3 XIAO Node

> OpenClaw node firmware for the Seeed Studio XIAO ESP32-C3 and XIAO Expansion Board, with BLE onboarding, OTP pairing, WiFi provisioning, and Cloudflare Access protected gateway connectivity.

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Build Firmware](https://img.shields.io/badge/CI-Build%20Firmware-1f6feb)](.github/workflows/build.yml)
[![Code Quality](https://img.shields.io/badge/CI-Code%20Quality-0a7f3f)](.github/workflows/lint.yml)
[![GitHub stars](https://img.shields.io/github/stars/chilu18/openclaw-esp32c3-xiao-node?style=social)](https://github.com/chilu18/openclaw-esp32c3-xiao-node/stargazers)
[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-HeySalad-FFDD00?logo=buy-me-a-coffee&logoColor=000000)](https://buymeacoffee.com/heysalad)

## What This Repo Contains

This repo contains the embedded firmware and supporting project files for a production-style OpenClaw peripheral node.

- `src/main.cpp`: production firmware for gateway-connected operation
- `src/hardware_smoke_test.cpp`: hardware validation firmware for display, RTC, speaker, and button checks
- `src/config.h.example`: local configuration template for secrets and device settings
- `src/config.defaults.h`: safe fallback defaults when `src/config.h` is not present
- `.github/workflows/`: build, lint, and release automation

## What The Firmware Does

- Exposes a BLE peripheral for onboarding from the Sally mobile app
- Generates and verifies 6-digit OTP pairing codes
- Accepts WiFi credentials over BLE and stores them in NVS
- Maintains a stable Ed25519 device identity in flash
- Connects to an OpenClaw gateway through Cloudflare Access
- Persists the issued OpenClaw device token after first successful pairing
- Receives `node.invoke.request` events and answers with `node.invoke.result`
- Drives the onboard OLED, buzzer, button, GPIO, and ADC features

## Hardware

Required hardware:

- Seeed Studio XIAO ESP32-C3
- Seeed Studio XIAO Expansion Board

Pin mapping:

| Function | Pin | GPIO | Notes |
|----------|-----|------|-------|
| OLED SDA | D4 | GPIO6 | I2C data |
| OLED SCL | D5 | GPIO7 | I2C clock |
| Button | D1 | GPIO3 | Active LOW |
| Buzzer | D3 | GPIO5 | PWM output |
| SD Card CS | D2 | GPIO4 | Reserved |
| ADC Input | A0 | GPIO2 | Analog input |

Hardware note:

- The OLED is driven with U8g2 software I2C on this board because that path is more reliable than the hardware-I2C U8g2 path on the current expansion-board setup.

## Architecture At A Glance

```text
Sally Mobile App
  - BLE scan
  - OTP entry
  - WiFi provisioning
          |
          v
OpenClaw ESP32-C3 Node
  - BLE peripheral
  - OLED status + OTP display
  - WiFi credentials in NVS
  - Stable Ed25519 device identity
          |
          v
Cloudflare Access
  - CF-Access-Client-Id
  - CF-Access-Client-Secret
          |
          v
OpenClaw Gateway
  - connect.challenge
  - challenge signature verification
  - device pairing approval
  - issued device token
```

## Quick Start

### 1. Clone the repo

```bash
git clone https://github.com/chilu18/openclaw-esp32c3-xiao-node.git
cd openclaw-esp32c3-xiao-node
```

### 2. Create local config

```bash
cp src/config.h.example src/config.h
```

Fill in at least:

- `GATEWAY_BOOTSTRAP_TOKEN`
- `CLOUDFLARE_ACCESS_CLIENT_ID`
- `CLOUDFLARE_ACCESS_CLIENT_SECRET`

Optional:

- `WIFI_SSID` and `WIFI_PASSWORD` for bench testing without BLE provisioning
- `GATEWAY_CA_CERT_PEM` if you want certificate pinning instead of relying on the default TLS path

### 3. Build the production firmware

```bash
pio run -e xiao_esp32c3
```

### 4. Flash the production firmware

```bash
pio run -e xiao_esp32c3 -t upload
pio device monitor
```

### 5. Pair and provision from mobile

From the Sally mobile app:

1. Scan and connect over BLE
2. Request OTP and enter the 6-digit code shown on the OLED
3. Send WiFi credentials over BLE
4. Let the node connect to the gateway
5. Approve the pairing request on the gateway host if required

### 6. Approve first-time node pairing

On the gateway host:

```bash
openclaw devices list
openclaw devices approve <request-id>
```

After first successful pairing, the firmware stores the issued device token in NVS and prefers that token on reconnect.

## Hardware Smoke Test

Use the smoke-test image when you want to validate hardware before debugging gateway or auth issues.

Build:

```bash
pio run -e hardware_smoke_test
```

Flash:

```bash
pio run -e hardware_smoke_test -t upload
pio device monitor
```

Expected checks:

- OLED detected on `0x3C`
- RTC detected on `0x51`
- speaker test tones
- button press and release events in serial output

## BLE Onboarding Protocol

BLE service:

- Service UUID: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- Command characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- Status characteristic: `beb5483e-36e1-4688-b7f5-ea07361b26a9`
- Device name: `OpenClaw-Node`

Recognized BLE commands:

- `OTP_REQUEST`
- `OTP_VERIFY:<6-digit-code>`
- `WIFI_SSID:{ssid}\nWIFI_PASS:{password}`
- `WIFI_STATUS`
- `WS_STATUS`
- `STATUS`
- `RESTART`

Typical BLE status responses:

- `OTP:<code>`
- `OTP_OK`
- `OTP_FAIL`
- `OTP_EXPIRED`
- `WIFI_CONNECTED`
- `WIFI_FAILED`
- `WIFI_DISCONNECTED`
- `WS_CONNECTED`
- `WS_DISCONNECTED`

## Gateway Auth Model

The node does not send a plain `connect` immediately after opening the socket.

Instead it:

1. Opens the websocket transport through Cloudflare Access
2. Waits for `connect.challenge`
3. Signs the v3 device payload with its Ed25519 private key
4. Sends authenticated `connect`
5. Persists `hello-ok.auth.deviceToken` after successful pairing

Device identity details:

- `device.id` is `sha256(raw_public_key)` in lowercase hex
- `device.publicKey` is base64url of the raw 32-byte Ed25519 public key
- the raw Ed25519 seed is stored in NVS for stable identity across reboots

## Supported Gateway Commands

The current production firmware supports:

- `esp.ping`
- `esp.gpio.read`
- `esp.gpio.write`
- `esp.adc.read`
- `esp.restart`
- `canvas.show`
- `canvas.clear`
- `audio.beep`
- `system.info`

It also emits:

- `input.button`

## Repository Layout

```text
openclaw-esp32c3-xiao-node/
├── .github/
│   └── workflows/
├── src/
│   ├── config.defaults.h
│   ├── config.h.example
│   ├── hardware_smoke_test.cpp
│   └── main.cpp
├── platformio.ini
├── README.md
├── CHANGELOG.md
├── CONTRIBUTING.md
├── SECURITY.md
└── LICENSE
```

## Validation Gates

Production firmware:

```bash
pio run -e xiao_esp32c3
```

Smoke-test firmware:

```bash
pio run -e hardware_smoke_test
```

Static checks:

```bash
pio check --skip-packages
```

## Release Flow

Tag-based release example:

```bash
git checkout main
git pull
git tag v1.1.0
git push origin v1.1.0
```

The current GitHub release workflow builds the production firmware automatically from tags.

## Security

- Never commit `src/config.h`
- Treat the gateway bootstrap token as a bootstrap secret only
- Rotate Cloudflare Access service tokens if exposed
- Prefer `GATEWAY_CA_CERT_PEM` for stronger TLS verification
- Redact serial logs before sharing if they contain device identifiers or network details

## Community

- Support the project: https://buymeacoffee.com/heysalad
- Star the repo: https://github.com/chilu18/openclaw-esp32c3-xiao-node/stargazers
- Open an issue: https://github.com/chilu18/openclaw-esp32c3-xiao-node/issues
- Submit a pull request: https://github.com/chilu18/openclaw-esp32c3-xiao-node/pulls

## References

External references:

- Cloudflare. "Publish a Self-Hosted Application to the Internet." *Cloudflare One Docs*. Accessed March 30, 2026. https://developers.cloudflare.com/cloudflare-one/access-controls/applications/http-apps/self-hosted-public-app/.
- Cloudflare. "Service Tokens." *Cloudflare One Docs*. Accessed March 30, 2026. https://developers.cloudflare.com/cloudflare-one/access-controls/service-credentials/service-tokens/.
- OpenClaw. "Gateway Protocol." *OpenClaw Docs*. Accessed March 30, 2026. https://docs.openclaw.ai/gateway/protocol.
- OpenClaw. "Gateway Runbook." *OpenClaw Docs*. Accessed March 30, 2026. https://docs.openclaw.ai/gateway/index.
- OpenClaw. "Nodes." *OpenClaw Docs*. Accessed March 30, 2026. https://docs.openclaw.ai/nodes.
- OpenClaw. "nodes." *OpenClaw CLI Reference*. Accessed March 30, 2026. https://docs.openclaw.ai/cli/nodes.

Internal implementation references:

- HeySalad Inc. *OpenClaw ESP32-C3 XIAO Node Firmware*. Authored by Peter Machona. Internal source code, March 30, 2026.
- HeySalad Inc. *OpenClaw ESP32-C3 XIAO Node Configuration Template*. Authored by Peter Machona. Internal source code, March 30, 2026.
- HeySalad Inc. *OpenClaw ESP32-C3 XIAO Node Default Configuration*. Authored by Peter Machona. Internal source code, March 30, 2026.
- HeySalad Inc. *OpenClaw ESP32-C3 XIAO Node Hardware Smoke Test*. Authored by Peter Machona. Internal source code, March 30, 2026.
- HeySalad Inc. *Protected Gateway Tunnel Configuration*. Authored by Peter Machona. Internal configuration source, March 30, 2026.
- HeySalad Inc. *Protected OpenClaw Gateway Configuration*. Authored by Peter Machona. Internal configuration source, March 30, 2026.

Public repo note:

- Internal references above intentionally redact hostnames, secrets, tokens, device identifiers, and environment-specific paths.

## License

MIT License. See [LICENSE](LICENSE).
