#ifndef CONFIG_DEFAULTS_H
#define CONFIG_DEFAULTS_H

// ============================================
// WiFi Configuration
// ============================================
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// ============================================
// Gateway + Cloudflare Access
// ============================================
#define GATEWAY_HOST "lucia-openclaw-dashboard.heysalad.app"
#define GATEWAY_PORT 443
#define GATEWAY_PATH "/"
#define GATEWAY_USE_SSL 1

// Shared bootstrap token used only until the gateway issues a device token.
#define GATEWAY_BOOTSTRAP_TOKEN ""

// Cloudflare Access service token for the public dashboard hostname.
#define CLOUDFLARE_ACCESS_CLIENT_ID ""
#define CLOUDFLARE_ACCESS_CLIENT_SECRET ""

// Optional: if you want certificate pinning instead of insecure TLS,
// define GATEWAY_CA_CERT_PEM in config.h with the PEM-encoded root or origin CA.

// ============================================
// Device Identity
// ============================================
#define DEVICE_CLIENT_ID "esp32c3-node"
#define DEVICE_DISPLAY_NAME "OpenClaw Node"
#define DEVICE_VERSION "0.2.0"
#define DEVICE_PLATFORM "embedded"
#define DEVICE_FAMILY "esp32c3"

// ============================================
// Hardware Pins (XIAO ESP32-C3 + Expansion Board)
// ============================================
#define PIN_BUTTON D1
#define PIN_BUZZER D3
#define PIN_I2C_SDA D4
#define PIN_I2C_SCL D5
#define PIN_SD_CS D2
#define PIN_ADC A0

// ============================================
// Display Configuration
// ============================================
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDRESS 0x3C

// ============================================
// Timing
// ============================================
#define DEBOUNCE_MS 50

#endif  // CONFIG_DEFAULTS_H
