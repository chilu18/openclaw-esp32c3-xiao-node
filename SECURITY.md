# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.x.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability in this firmware, please report it responsibly:

1. **Do NOT open a public issue**
2. Email security concerns to: security@heysalad.com
3. Include:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Any suggested fixes

## Security Considerations

### WiFi Credentials
- WiFi credentials are stored in `config.h` which is gitignored
- Never commit `config.h` to version control
- Credentials are stored in plain text on the ESP32 flash

### Gateway Communication
- WebSocket connection is currently unencrypted (ws://)
- For production, use a secure network or implement TLS
- Gateway token (if used) is transmitted in the clear

### Physical Security
- Anyone with physical access to the device can:
  - Read flash contents (including WiFi credentials)
  - Reflash the firmware
  - Connect to the serial console

### Recommendations

1. **Network Isolation**: Run OpenClaw nodes on a dedicated IoT VLAN
2. **Secure Gateway**: Place the gateway behind a firewall
3. **Regular Updates**: Keep firmware updated for security patches
4. **Physical Access**: Secure devices from unauthorized physical access

## Known Limitations

- No TLS/SSL support (ESP32-C3 memory constraints)
- No secure boot enabled by default
- Plain text credential storage

These are acceptable tradeoffs for a development/hobbyist device but should be addressed for production deployments.
