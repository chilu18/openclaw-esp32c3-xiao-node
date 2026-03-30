#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <base64.h>
#include <esp32-hal-ledc.h>
#include <esp_system.h>
#include <sodium.h>
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_sign.h>
#include <sodium/utils.h>

#if __has_include("config.h")
#include "config.h"
#else
#include "config.defaults.h"
#endif

#ifndef DEVICE_CLIENT_ID
#ifdef DEVICE_NAME
#define DEVICE_CLIENT_ID DEVICE_NAME
#else
#define DEVICE_CLIENT_ID "esp32c3-node"
#endif
#endif

#ifndef DEVICE_DISPLAY_NAME
#define DEVICE_DISPLAY_NAME "OpenClaw Node"
#endif

#ifndef DEVICE_VERSION
#define DEVICE_VERSION "0.2.0"
#endif

#ifndef DEVICE_FAMILY
#ifdef DEVICE_TYPE
#define DEVICE_FAMILY DEVICE_TYPE
#else
#define DEVICE_FAMILY "esp32c3"
#endif
#endif

#ifndef DEVICE_PLATFORM
#define DEVICE_PLATFORM "embedded"
#endif

#ifndef GATEWAY_BOOTSTRAP_TOKEN
#ifdef GATEWAY_TOKEN
#define GATEWAY_BOOTSTRAP_TOKEN GATEWAY_TOKEN
#else
#define GATEWAY_BOOTSTRAP_TOKEN ""
#endif
#endif

#ifndef GATEWAY_PATH
#define GATEWAY_PATH "/"
#endif

#ifndef GATEWAY_USE_SSL
#define GATEWAY_USE_SSL 1
#endif

#ifndef GATEWAY_PORT
#define GATEWAY_PORT 443
#endif

#ifndef CLOUDFLARE_ACCESS_CLIENT_ID
#define CLOUDFLARE_ACCESS_CLIENT_ID ""
#endif

#ifndef CLOUDFLARE_ACCESS_CLIENT_SECRET
#define CLOUDFLARE_ACCESS_CLIENT_SECRET ""
#endif

#ifndef DEBOUNCE_MS
#define DEBOUNCE_MS 50
#endif

namespace {

constexpr char kPrefsNamespace[] = "openclaw";
constexpr char kPrefsWifiSsidKey[] = "wifi_ssid";
constexpr char kPrefsWifiPassKey[] = "wifi_pass";
constexpr char kPrefsDeviceSeedKey[] = "dev_seed";
constexpr char kPrefsDeviceTokenKey[] = "dev_token";

constexpr char kBleDeviceName[] = "OpenClaw-Node";
constexpr char kBleServiceUuid[] = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
constexpr char kBleCommandCharacteristicUuid[] =
    "beb5483e-36e1-4688-b7f5-ea07361b26a8";
constexpr char kBleStatusCharacteristicUuid[] =
    "beb5483e-36e1-4688-b7f5-ea07361b26a9";

constexpr uint32_t kOtpTtlMs = 30000;
constexpr uint32_t kWifiConnectTimeoutMs = 20000;
constexpr uint32_t kConnectChallengeTimeoutMs = 10000;
constexpr uint32_t kReconnectInitialMs = 1000;
constexpr uint32_t kReconnectMaxMs = 30000;
constexpr uint32_t kReconnectJitterDivisor = 5;
constexpr uint32_t kDefaultTickIntervalMs = 30000;
constexpr uint32_t kDisplayRefreshMs = 250;
constexpr uint32_t kSpeakerChannel = 0;

Preferences prefs;
WebSocketsClient webSocket;
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
    U8G2_R0, PIN_I2C_SCL, PIN_I2C_SDA, U8X8_PIN_NONE);

NimBLEServer *bleServer = nullptr;
NimBLECharacteristic *bleCommandCharacteristic = nullptr;
NimBLECharacteristic *bleStatusCharacteristic = nullptr;

bool displayDetected = false;
bool bleClientConnected = false;
bool wifiConnected = false;
bool gatewayTransportConnected = false;
bool gatewayAuthenticated = false;
bool waitingForConnectChallenge = false;
bool connectRequestPending = false;
bool pairingRequired = false;
bool lastButtonState = HIGH;

unsigned long lastButtonPollMs = 0;
unsigned long lastDisplayRefreshMs = 0;
unsigned long connectChallengeDeadlineMs = 0;
unsigned long connectChallengeReceivedAtMs = 0;
unsigned long lastGatewayTickMs = 0;
unsigned long nextReconnectAttemptMs = 0;

uint32_t reconnectDelayMs = kReconnectInitialMs;
uint32_t gatewayTickIntervalMs = kDefaultTickIntervalMs;
uint32_t requestCounter = 0;

uint8_t deviceSeed[crypto_sign_SEEDBYTES] = {0};
uint8_t devicePublicKey[crypto_sign_PUBLICKEYBYTES] = {0};
uint8_t deviceSecretKey[crypto_sign_SECRETKEYBYTES] = {0};

char otpCode[7] = {0};
unsigned long otpExpiryMs = 0;

String wifiSsid;
String wifiPassword;
String deviceId;
String devicePublicKeyBase64Url;
String persistedDeviceToken;
String activeAuthToken;
String connectNonce;
uint64_t connectChallengeTsMs = 0;
String pendingConnectRequestId;
String gatewayExtraHeaders;

enum class DisplayMode {
  Status,
  Message,
  Otp,
  Canvas,
};

DisplayMode displayMode = DisplayMode::Status;
String displayTitle = DEVICE_DISPLAY_NAME;
String displayLine1 = "";
String displayLine2 = "";
String messageTitle = "";
String messageLine1 = "";
String messageLine2 = "";
String canvasText = "";
int canvasX = 0;
int canvasY = 16;
int canvasSize = 1;
bool canvasClear = true;

bool probeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

String nextRequestId(const char *prefix) {
  ++requestCounter;
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%s-%lu", prefix,
           static_cast<unsigned long>(requestCounter));
  return String(buffer);
}

String toHexLower(const uint8_t *data, size_t length) {
  static const char kHex[] = "0123456789abcdef";
  String out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; ++i) {
    out += kHex[(data[i] >> 4) & 0x0F];
    out += kHex[data[i] & 0x0F];
  }
  return out;
}

String toBase64Url(const uint8_t *data, size_t length) {
  size_t encodedLen = sodium_base64_encoded_len(
      length, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  std::unique_ptr<char[]> buffer(new char[encodedLen]);
  sodium_bin2base64(buffer.get(), encodedLen, data, length,
                    sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  return String(buffer.get());
}

void saveStringPref(const char *key, const String &value) {
  prefs.putString(key, value);
}

String loadStringPref(const char *key) {
  return prefs.getString(key, "");
}

void playTone(int frequency, int durationMs) {
  ledcSetup(kSpeakerChannel, frequency, 10);
  ledcAttachPin(PIN_BUZZER, kSpeakerChannel);
  ledcWriteTone(kSpeakerChannel, frequency);
  delay(durationMs);
  ledcWriteTone(kSpeakerChannel, 0);
  ledcDetachPin(PIN_BUZZER);
}

void sendBleStatus(const String &status) {
  Serial.printf("[BLE] %s\n", status.c_str());
  if (!bleStatusCharacteristic) {
    return;
  }

  bleStatusCharacteristic->setValue(status.c_str());
  if (bleClientConnected) {
    bleStatusCharacteristic->notify();
  }
}

void setStatusScreen(const String &title, const String &line1 = "",
                     const String &line2 = "") {
  displayMode = DisplayMode::Status;
  displayTitle = title;
  displayLine1 = line1;
  displayLine2 = line2;
}

void setMessageScreen(const String &title, const String &line1 = "",
                      const String &line2 = "") {
  displayMode = DisplayMode::Message;
  messageTitle = title;
  messageLine1 = line1;
  messageLine2 = line2;
}

void setCanvasScreen(const String &text, int x, int y, int size, bool clear) {
  displayMode = DisplayMode::Canvas;
  canvasText = text;
  canvasX = x;
  canvasY = y;
  canvasSize = size;
  canvasClear = clear;
}

String gatewayStateLabel() {
  if (gatewayAuthenticated) {
    return "GW:OK";
  }
  if (gatewayTransportConnected) {
    return "GW:HS";
  }
  return "GW:--";
}

void refreshDisplay() {
  if (!displayDetected) {
    return;
  }

  u8g2.clearBuffer();

  if (displayMode == DisplayMode::Canvas) {
    if (!canvasClear) {
      u8g2.sendBuffer();
    }
    if (canvasSize >= 3) {
      u8g2.setFont(u8g2_font_logisoso16_tf);
    } else if (canvasSize == 2) {
      u8g2.setFont(u8g2_font_10x20_tf);
    } else {
      u8g2.setFont(u8g2_font_6x10_tf);
    }
    u8g2.drawUTF8(canvasX, canvasY, canvasText.c_str());
    u8g2.sendBuffer();
    return;
  }

  if (displayMode == DisplayMode::Otp && otpCode[0] != '\0' &&
      millis() < otpExpiryMs) {
    const unsigned long remainingMs = otpExpiryMs - millis();
    const unsigned long remainingSeconds = (remainingMs + 999) / 1000;

    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawUTF8(0, 12, "OpenClaw Code");
    u8g2.drawUTF8(0, 24, "Enter this in Sally");
    u8g2.setFont(u8g2_font_logisoso16_tn);
    u8g2.drawUTF8(6, 48, otpCode);
    u8g2.setFont(u8g2_font_5x7_tf);
    String footer = "Expires in " + String(remainingSeconds) + "s";
    u8g2.drawUTF8(0, 62, footer.c_str());
    u8g2.sendBuffer();
    return;
  }

  u8g2.setFont(u8g2_font_6x10_tf);
  const String title = displayMode == DisplayMode::Message ? messageTitle
                                                           : displayTitle;
  const String line1 = displayMode == DisplayMode::Message ? messageLine1
                                                           : displayLine1;
  const String line2 = displayMode == DisplayMode::Message ? messageLine2
                                                           : displayLine2;

  u8g2.drawUTF8(0, 12, title.c_str());
  u8g2.drawUTF8(0, 28, line1.c_str());
  u8g2.drawUTF8(0, 44, line2.c_str());
  u8g2.drawLine(0, 54, 128, 54);

  String footer = wifiConnected ? "WiFi:OK " : "WiFi:-- ";
  footer += gatewayStateLabel();
  if (bleClientConnected) {
    footer += " BLE";
  }
  u8g2.drawUTF8(0, 63, footer.c_str());
  u8g2.sendBuffer();
}

void setupDisplay() {
  displayDetected = probeAddress(OLED_ADDRESS);
  if (!displayDetected) {
    Serial.println("[OLED] No display detected at configured I2C address");
    return;
  }

  u8g2.begin();
  refreshDisplay();
  Serial.println("[OLED] Display initialized");
}

void clearOtpCode() {
  memset(otpCode, 0, sizeof(otpCode));
  otpExpiryMs = 0;
  if (displayMode == DisplayMode::Otp) {
    setStatusScreen(DEVICE_DISPLAY_NAME, wifiConnected ? "WiFi ready"
                                                       : "Waiting for WiFi",
                    gatewayAuthenticated ? "Gateway linked"
                                         : "Gateway offline");
  }
}

void generateOtpCode() {
  const uint32_t raw = esp_random() % 1000000UL;
  snprintf(otpCode, sizeof(otpCode), "%06lu", static_cast<unsigned long>(raw));
  otpExpiryMs = millis() + kOtpTtlMs;
  displayMode = DisplayMode::Otp;
  Serial.printf("[OTP] Generated %s\n", otpCode);
  playTone(1200, 60);
}

String currentOtpStatusForCode(const String &candidate) {
  if (otpCode[0] == '\0') {
    return "OTP_EXPIRED";
  }
  if (millis() > otpExpiryMs) {
    clearOtpCode();
    return "OTP_EXPIRED";
  }
  if (candidate.equals(String(otpCode))) {
    clearOtpCode();
    return "OTP_OK";
  }
  return "OTP_FAIL";
}

bool loadOrCreateDeviceIdentity() {
  size_t seedLength = prefs.getBytesLength(kPrefsDeviceSeedKey);
  if (seedLength == crypto_sign_SEEDBYTES) {
    prefs.getBytes(kPrefsDeviceSeedKey, deviceSeed, crypto_sign_SEEDBYTES);
  } else {
    esp_fill_random(deviceSeed, crypto_sign_SEEDBYTES);
    prefs.putBytes(kPrefsDeviceSeedKey, deviceSeed, crypto_sign_SEEDBYTES);
  }

  if (crypto_sign_seed_keypair(devicePublicKey, deviceSecretKey, deviceSeed) !=
      0) {
    Serial.println("[AUTH] Failed to derive device keypair");
    return false;
  }

  uint8_t deviceHash[crypto_hash_sha256_BYTES] = {0};
  crypto_hash_sha256(deviceHash, devicePublicKey, crypto_sign_PUBLICKEYBYTES);
  deviceId = toHexLower(deviceHash, sizeof(deviceHash));
  devicePublicKeyBase64Url =
      toBase64Url(devicePublicKey, crypto_sign_PUBLICKEYBYTES);

  Serial.printf("[AUTH] device.id=%s\n", deviceId.c_str());
  return true;
}

void loadSettings() {
  wifiSsid = loadStringPref(kPrefsWifiSsidKey);
  wifiPassword = loadStringPref(kPrefsWifiPassKey);
  persistedDeviceToken = loadStringPref(kPrefsDeviceTokenKey);
}

bool connectWiFi(unsigned long timeoutMs = kWifiConnectTimeoutMs) {
  if (wifiSsid.isEmpty()) {
    wifiConnected = false;
    setStatusScreen(DEVICE_DISPLAY_NAME, "Waiting for WiFi", "Provision via BLE");
    return false;
  }

  setStatusScreen("WiFi", "Connecting...", wifiSsid);
  refreshDisplay();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < timeoutMs) {
    delay(250);
  }

  wifiConnected = WiFi.status() == WL_CONNECTED;
  if (wifiConnected) {
    String ip = WiFi.localIP().toString();
    Serial.printf("[WiFi] Connected: %s\n", ip.c_str());
    setStatusScreen("WiFi Connected", ip, WiFi.SSID());
    playTone(1500, 80);
  } else {
    Serial.println("[WiFi] Connection failed");
    setMessageScreen("WiFi Failed", "Check credentials", "Retry in Sally");
    playTone(500, 160);
  }

  refreshDisplay();
  return wifiConnected;
}

void disconnectGateway(const char *reason) {
  if (reason && *reason) {
    Serial.printf("[GW] Disconnecting: %s\n", reason);
  }
  webSocket.disconnect();
  gatewayTransportConnected = false;
  gatewayAuthenticated = false;
  waitingForConnectChallenge = false;
  connectRequestPending = false;
  pendingConnectRequestId = "";
}

void scheduleReconnect(bool authFailure = false) {
  const uint32_t jitterBase =
      reconnectDelayMs / kReconnectJitterDivisor + 1;
  const uint32_t jitter = esp_random() % jitterBase;
  nextReconnectAttemptMs = millis() + reconnectDelayMs + jitter;

  if (authFailure) {
    reconnectDelayMs = min<uint32_t>(reconnectDelayMs * 2, kReconnectMaxMs);
  } else {
    reconnectDelayMs = min<uint32_t>(reconnectDelayMs * 2, kReconnectMaxMs);
  }
}

String resolveGatewayToken() {
  if (!persistedDeviceToken.isEmpty()) {
    return persistedDeviceToken;
  }
  return String(GATEWAY_BOOTSTRAP_TOKEN);
}

void prepareGatewayHeaders() {
  gatewayExtraHeaders = "";
  if (strlen(CLOUDFLARE_ACCESS_CLIENT_ID) > 0) {
    gatewayExtraHeaders += "CF-Access-Client-Id: ";
    gatewayExtraHeaders += CLOUDFLARE_ACCESS_CLIENT_ID;
    gatewayExtraHeaders += "\r\n";
  }
  if (strlen(CLOUDFLARE_ACCESS_CLIENT_SECRET) > 0) {
    gatewayExtraHeaders += "CF-Access-Client-Secret: ";
    gatewayExtraHeaders += CLOUDFLARE_ACCESS_CLIENT_SECRET;
    gatewayExtraHeaders += "\r\n";
  }
}

void beginGatewayConnect() {
  if (!wifiConnected) {
    return;
  }

  activeAuthToken = resolveGatewayToken();
  if (activeAuthToken.isEmpty()) {
    setMessageScreen("Gateway Auth", "Missing token", "Set config.h");
    refreshDisplay();
    return;
  }

  waitingForConnectChallenge = true;
  connectRequestPending = false;
  pairingRequired = false;
  connectNonce = "";
  connectChallengeTsMs = 0;
  pendingConnectRequestId = "";
  connectChallengeDeadlineMs = millis() + kConnectChallengeTimeoutMs;

  prepareGatewayHeaders();
  webSocket.setExtraHeaders(gatewayExtraHeaders.isEmpty()
                                ? nullptr
                                : gatewayExtraHeaders.c_str());
  webSocket.enableHeartbeat(20000, 5000, 2);

  setStatusScreen("Gateway", "Opening socket...", GATEWAY_HOST);
  refreshDisplay();

#if GATEWAY_USE_SSL
#ifdef GATEWAY_CA_CERT_PEM
  webSocket.beginSslWithCA(GATEWAY_HOST, GATEWAY_PORT, GATEWAY_PATH,
                           GATEWAY_CA_CERT_PEM);
#else
  webSocket.beginSSL(GATEWAY_HOST, GATEWAY_PORT, GATEWAY_PATH);
#endif
#else
  webSocket.begin(GATEWAY_HOST, GATEWAY_PORT, GATEWAY_PATH);
#endif
}

uint64_t resolveSignedAtMs() {
  if (connectChallengeTsMs == 0) {
    return 0;
  }
  const unsigned long deltaMs = millis() - connectChallengeReceivedAtMs;
  return connectChallengeTsMs + static_cast<uint64_t>(deltaMs);
}

String buildDeviceAuthPayload(const String &token, const String &nonce,
                              uint64_t signedAtMs) {
  String payload = "v3|";
  payload += deviceId;
  payload += "|";
  payload += DEVICE_CLIENT_ID;
  payload += "|node|node||";
  payload += String(static_cast<unsigned long long>(signedAtMs));
  payload += "|";
  payload += token;
  payload += "|";
  payload += nonce;
  payload += "|";
  payload += DEVICE_PLATFORM;
  payload += "|";
  payload += DEVICE_FAMILY;
  return payload;
}

String signDeviceAuthPayload(const String &payload) {
  unsigned char signature[crypto_sign_BYTES] = {0};
  unsigned long long signatureLength = 0;
  if (crypto_sign_detached(signature, &signatureLength,
                           reinterpret_cast<const unsigned char *>(
                               payload.c_str()),
                           payload.length(), deviceSecretKey) != 0) {
    return "";
  }
  return toBase64Url(signature, signatureLength);
}

void sendConnectRequest() {
  if (!waitingForConnectChallenge || connectNonce.isEmpty()) {
    return;
  }

  const uint64_t signedAtMs = resolveSignedAtMs();
  if (signedAtMs == 0) {
    setMessageScreen("Gateway Auth", "Missing challenge ts", "Retrying");
    refreshDisplay();
    disconnectGateway("challenge missing timestamp");
    scheduleReconnect(true);
    return;
  }

  String payloadToSign =
      buildDeviceAuthPayload(activeAuthToken, connectNonce, signedAtMs);
  String signature = signDeviceAuthPayload(payloadToSign);
  if (signature.isEmpty()) {
    setMessageScreen("Gateway Auth", "Sign failed", "Retrying");
    refreshDisplay();
    disconnectGateway("signature failure");
    scheduleReconnect(true);
    return;
  }

  JsonDocument doc;
  const String requestId = nextRequestId("connect");
  pendingConnectRequestId = requestId;
  connectRequestPending = true;
  waitingForConnectChallenge = false;

  doc["type"] = "req";
  doc["id"] = requestId;
  doc["method"] = "connect";

  JsonObject params = doc["params"].to<JsonObject>();
  params["minProtocol"] = 3;
  params["maxProtocol"] = 3;

  JsonObject client = params["client"].to<JsonObject>();
  client["id"] = DEVICE_CLIENT_ID;
  client["version"] = DEVICE_VERSION;
  client["platform"] = DEVICE_PLATFORM;
  client["deviceFamily"] = DEVICE_FAMILY;
  client["mode"] = "node";

  params["role"] = "node";
  params["scopes"].to<JsonArray>();

  JsonArray caps = params["caps"].to<JsonArray>();
  caps.add("display");
  caps.add("audio");
  caps.add("button");
  caps.add("gpio");
  caps.add("adc");
  caps.add("i2c");
  caps.add("wifi");
  caps.add("ota");

  JsonArray commands = params["commands"].to<JsonArray>();
  commands.add("esp.ping");
  commands.add("esp.gpio.read");
  commands.add("esp.gpio.write");
  commands.add("esp.adc.read");
  commands.add("esp.restart");
  commands.add("canvas.show");
  commands.add("canvas.clear");
  commands.add("audio.beep");
  commands.add("system.info");

  JsonObject permissions = params["permissions"].to<JsonObject>();
  permissions["display.write"] = true;
  permissions["audio.beep"] = true;
  permissions["gpio.write"] = true;
  permissions["wifi.provision"] = true;

  JsonObject auth = params["auth"].to<JsonObject>();
  auth["token"] = activeAuthToken;

  JsonObject device = params["device"].to<JsonObject>();
  device["id"] = deviceId;
  device["publicKey"] = devicePublicKeyBase64Url;
  device["signature"] = signature;
  device["signedAt"] = static_cast<uint64_t>(signedAtMs);
  device["nonce"] = connectNonce;

  String message;
  serializeJson(doc, message);
  Serial.printf("[GW] Sending connect as %s\n", deviceId.c_str());
  webSocket.sendTXT(message);
  setStatusScreen("Gateway", "Authenticating...", persistedDeviceToken.isEmpty()
                                                 ? "Bootstrap token"
                                                 : "Device token");
  refreshDisplay();
}

void sendGatewayMethod(const char *method, JsonDocument &paramsDoc) {
  if (!gatewayAuthenticated) {
    return;
  }

  JsonDocument requestDoc;
  requestDoc["type"] = "req";
  requestDoc["id"] = nextRequestId("node");
  requestDoc["method"] = method;
  requestDoc["params"] = paramsDoc.as<JsonVariantConst>();

  String raw;
  serializeJson(requestDoc, raw);
  webSocket.sendTXT(raw);
}

void sendNodeInvokeResult(const String &id, bool ok, JsonDocument *payloadDoc,
                          const char *errorCode,
                          const char *errorMessage) {
  JsonDocument paramsDoc;
  paramsDoc["id"] = id;
  paramsDoc["nodeId"] = deviceId;
  paramsDoc["ok"] = ok;

  if (payloadDoc) {
    paramsDoc["payload"] = payloadDoc->as<JsonVariantConst>();
  }

  if (!ok) {
    JsonObject error = paramsDoc["error"].to<JsonObject>();
    if (errorCode && *errorCode) {
      error["code"] = errorCode;
    }
    if (errorMessage && *errorMessage) {
      error["message"] = errorMessage;
    }
  }

  sendGatewayMethod("node.invoke.result", paramsDoc);
}

void sendNodeEvent(const String &event, JsonDocument &payloadDoc) {
  JsonDocument paramsDoc;
  paramsDoc["event"] = event;
  paramsDoc["payload"] = payloadDoc.as<JsonVariantConst>();
  sendGatewayMethod("node.event", paramsDoc);
}

void handleButtonEvent(bool pressed) {
  if (!gatewayAuthenticated) {
    return;
  }

  JsonDocument payloadDoc;
  payloadDoc["state"] = pressed ? "pressed" : "released";
  payloadDoc["button"] = "main";
  payloadDoc["timestampMs"] = millis();
  sendNodeEvent("input.button", payloadDoc);
}

bool parseWifiProvisioningCommand(const String &command, String &ssidOut,
                                  String &passwordOut) {
  if (command.startsWith("WIFI_SSID:")) {
    const int newlineIndex = command.indexOf('\n');
    if (newlineIndex < 0) {
      return false;
    }

    ssidOut = command.substring(10, newlineIndex);
    String passwordLine = command.substring(newlineIndex + 1);
    if (!passwordLine.startsWith("WIFI_PASS:")) {
      return false;
    }

    passwordOut = passwordLine.substring(10);
    return true;
  }

  if (command.startsWith("WIFI_CONFIG:")) {
    const int secondColon = command.indexOf(':', 12);
    if (secondColon < 0) {
      return false;
    }
    ssidOut = command.substring(12, secondColon);
    passwordOut = command.substring(secondColon + 1);
    return true;
  }

  return false;
}

bool applyWifiProvisioning(const String &ssid, const String &password) {
  wifiSsid = ssid;
  wifiPassword = password;
  saveStringPref(kPrefsWifiSsidKey, wifiSsid);
  saveStringPref(kPrefsWifiPassKey, wifiPassword);

  disconnectGateway("wifi reprovision");
  WiFi.disconnect(true, false);
  delay(250);

  if (connectWiFi()) {
    sendBleStatus("WIFI_CONNECTED");
    reconnectDelayMs = kReconnectInitialMs;
    nextReconnectAttemptMs = millis();
    return true;
  }

  sendBleStatus("WIFI_FAILED");
  scheduleReconnect(true);
  return false;
}

void sendBleSummary() {
  String summary = "WIFI:";
  summary += wifiConnected ? "1" : "0";
  summary += "|GW:";
  summary += gatewayAuthenticated ? "1" : "0";
  summary += "|ID:";
  summary += deviceId;
  sendBleStatus(summary);
}

void handleBleCommand(const String &command) {
  Serial.printf("[BLE] cmd=%s\n", command.c_str());

  if (command.equalsIgnoreCase("OTP_REQUEST")) {
    generateOtpCode();
    sendBleStatus(String("OTP:") + otpCode);
    refreshDisplay();
    return;
  }

  if (command.startsWith("OTP_VERIFY:")) {
    String entered = command.substring(11);
    entered.trim();
    const String status = currentOtpStatusForCode(entered);
    if (status == "OTP_OK") {
      playTone(1600, 80);
    } else if (status == "OTP_FAIL") {
      playTone(600, 120);
    }
    sendBleStatus(status);
    refreshDisplay();
    return;
  }

  String ssid;
  String password;
  if (parseWifiProvisioningCommand(command, ssid, password)) {
    applyWifiProvisioning(ssid, password);
    refreshDisplay();
    return;
  }

  if (command.equalsIgnoreCase("WIFI_STATUS")) {
    sendBleStatus(wifiConnected ? "WIFI_CONNECTED" : "WIFI_DISCONNECTED");
    return;
  }

  if (command.equalsIgnoreCase("WS_STATUS")) {
    sendBleStatus(gatewayAuthenticated ? "WS_CONNECTED" : "WS_DISCONNECTED");
    return;
  }

  if (command.equalsIgnoreCase("STATUS")) {
    sendBleSummary();
    return;
  }

  if (command.equalsIgnoreCase("RESTART")) {
    sendBleStatus("RESTARTING");
    delay(150);
    ESP.restart();
  }

  sendBleStatus("UNKNOWN_COMMAND");
}

void handleGatewayHello(JsonVariantConst payload) {
  gatewayAuthenticated = true;
  gatewayTransportConnected = true;
  connectRequestPending = false;
  pendingConnectRequestId = "";
  reconnectDelayMs = kReconnectInitialMs;
  nextReconnectAttemptMs = millis() + kReconnectInitialMs;
  gatewayTickIntervalMs =
      payload["policy"]["tickIntervalMs"] | kDefaultTickIntervalMs;
  lastGatewayTickMs = millis();

  String issuedDeviceToken =
      payload["auth"]["deviceToken"] | String();
  if (!issuedDeviceToken.isEmpty() && issuedDeviceToken != persistedDeviceToken) {
    persistedDeviceToken = issuedDeviceToken;
    saveStringPref(kPrefsDeviceTokenKey, persistedDeviceToken);
    Serial.println("[GW] Persisted new device token");
  }

  setStatusScreen("Gateway Ready", deviceId.substring(0, 12),
                  WiFi.localIP().toString());
  refreshDisplay();
  playTone(1800, 80);
}

void handleConnectError(JsonVariantConst error) {
  const String message = error["message"] | "Gateway connect failed";
  const String detailCode = error["details"]["code"] | "";
  Serial.printf("[GW] connect failed: %s (%s)\n", message.c_str(),
                detailCode.c_str());

  if (detailCode == "PAIRING_REQUIRED") {
    pairingRequired = true;
    setMessageScreen("Pairing Required", "Approve in gateway",
                     deviceId.substring(0, 12));
  } else {
    setMessageScreen("Gateway Error", detailCode.isEmpty() ? message : detailCode,
                     message);
  }
  refreshDisplay();

  disconnectGateway("connect failed");
  scheduleReconnect(true);
}

void handleGatewayResponse(JsonVariantConst root) {
  const String responseId = root["id"] | "";
  if (responseId.isEmpty()) {
    return;
  }

  if (responseId != pendingConnectRequestId) {
    return;
  }

  if (root["ok"] | false) {
    handleGatewayHello(root["payload"]);
  } else {
    handleConnectError(root["error"]);
  }
}

JsonDocument parseInvokeParams(JsonVariantConst payload) {
  JsonDocument paramsDoc;
  if (!payload.is<JsonVariantConst>()) {
    return paramsDoc;
  }

  if (!payload["params"].isNull()) {
    paramsDoc.set(payload["params"]);
    return paramsDoc;
  }

  const String paramsJson = payload["paramsJSON"] | "";
  if (!paramsJson.isEmpty()) {
    deserializeJson(paramsDoc, paramsJson);
  }

  return paramsDoc;
}

void handleCommandCanvasShow(const String &invokeId,
                             JsonVariantConst paramsVariant) {
  const String text = paramsVariant["text"] | "";
  const int x = paramsVariant["x"] | 0;
  const int y = paramsVariant["y"] | 16;
  const int size = paramsVariant["size"] | 1;
  const bool clear = paramsVariant["clear"].isNull()
                         ? true
                         : static_cast<bool>(paramsVariant["clear"]);

  setCanvasScreen(text, x, y, size, clear);
  refreshDisplay();

  JsonDocument payloadDoc;
  payloadDoc["shown"] = true;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandCanvasClear(const String &invokeId) {
  setStatusScreen(DEVICE_DISPLAY_NAME, wifiConnected ? "WiFi ready"
                                                     : "Waiting for WiFi",
                  gatewayAuthenticated ? "Gateway linked"
                                       : "Gateway offline");
  refreshDisplay();

  JsonDocument payloadDoc;
  payloadDoc["cleared"] = true;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandAudioBeep(const String &invokeId,
                            JsonVariantConst paramsVariant) {
  const int frequency = paramsVariant["frequency"] | 1000;
  const int durationMs = paramsVariant["duration"] | 120;
  playTone(frequency, durationMs);

  JsonDocument payloadDoc;
  payloadDoc["frequency"] = frequency;
  payloadDoc["durationMs"] = durationMs;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandSystemInfo(const String &invokeId) {
  JsonDocument payloadDoc;
  payloadDoc["deviceId"] = deviceId;
  payloadDoc["clientId"] = DEVICE_CLIENT_ID;
  payloadDoc["platform"] = DEVICE_PLATFORM;
  payloadDoc["deviceFamily"] = DEVICE_FAMILY;
  payloadDoc["ip"] = wifiConnected ? WiFi.localIP().toString() : "";
  payloadDoc["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
  payloadDoc["freeHeap"] = ESP.getFreeHeap();
  payloadDoc["uptimeMs"] = millis();
  payloadDoc["displayDetected"] = displayDetected;
  payloadDoc["wifiConnected"] = wifiConnected;
  payloadDoc["gatewayConnected"] = gatewayAuthenticated;
  payloadDoc["bleConnected"] = bleClientConnected;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandEspPing(const String &invokeId) {
  JsonDocument payloadDoc;
  payloadDoc["pong"] = true;
  payloadDoc["uptimeMs"] = millis();
  payloadDoc["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
  payloadDoc["freeHeap"] = ESP.getFreeHeap();
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandGpioRead(const String &invokeId,
                           JsonVariantConst paramsVariant) {
  const int pin = paramsVariant["pin"] | static_cast<int>(PIN_BUTTON);
  pinMode(pin, INPUT);
  const int value = digitalRead(pin);

  JsonDocument payloadDoc;
  payloadDoc["pin"] = pin;
  payloadDoc["value"] = value;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandGpioWrite(const String &invokeId,
                            JsonVariantConst paramsVariant) {
  const int pin = paramsVariant["pin"] | static_cast<int>(PIN_BUZZER);
  const bool value = paramsVariant["value"] | false;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, value ? HIGH : LOW);

  JsonDocument payloadDoc;
  payloadDoc["pin"] = pin;
  payloadDoc["value"] = value;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandAdcRead(const String &invokeId,
                          JsonVariantConst paramsVariant) {
  const int pin = paramsVariant["pin"] | static_cast<int>(PIN_ADC);
  const int value = analogRead(pin);

  JsonDocument payloadDoc;
  payloadDoc["pin"] = pin;
  payloadDoc["value"] = value;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
}

void handleCommandRestart(const String &invokeId) {
  JsonDocument payloadDoc;
  payloadDoc["restarting"] = true;
  sendNodeInvokeResult(invokeId, true, &payloadDoc, nullptr, nullptr);
  delay(150);
  ESP.restart();
}

void handleNodeInvoke(JsonVariantConst payload) {
  const String invokeId = payload["id"] | "";
  const String targetNodeId = payload["nodeId"] | "";
  const String command = payload["command"] | "";

  if (invokeId.isEmpty() || targetNodeId.isEmpty() || command.isEmpty()) {
    return;
  }

  if (targetNodeId != deviceId) {
    sendNodeInvokeResult(invokeId, false, nullptr, "WRONG_NODE",
                         "invoke addressed to a different device");
    return;
  }

  JsonDocument paramsDoc = parseInvokeParams(payload);
  const JsonVariantConst paramsVariant = paramsDoc.as<JsonVariantConst>();

  Serial.printf("[GW] invoke %s\n", command.c_str());

  if (command == "esp.ping") {
    handleCommandEspPing(invokeId);
    return;
  }
  if (command == "esp.gpio.read") {
    handleCommandGpioRead(invokeId, paramsVariant);
    return;
  }
  if (command == "esp.gpio.write") {
    handleCommandGpioWrite(invokeId, paramsVariant);
    return;
  }
  if (command == "esp.adc.read") {
    handleCommandAdcRead(invokeId, paramsVariant);
    return;
  }
  if (command == "esp.restart") {
    handleCommandRestart(invokeId);
    return;
  }
  if (command == "canvas.show") {
    handleCommandCanvasShow(invokeId, paramsVariant);
    return;
  }
  if (command == "canvas.clear") {
    handleCommandCanvasClear(invokeId);
    return;
  }
  if (command == "audio.beep") {
    handleCommandAudioBeep(invokeId, paramsVariant);
    return;
  }
  if (command == "system.info") {
    handleCommandSystemInfo(invokeId);
    return;
  }

  sendNodeInvokeResult(invokeId, false, nullptr, "NOT_IMPLEMENTED",
                       "command not implemented");
}

void handleGatewayEvent(JsonVariantConst root) {
  const String eventName = root["event"] | "";
  if (eventName.isEmpty()) {
    return;
  }

  if (eventName == "connect.challenge") {
    const JsonVariantConst payload = root["payload"];
    const String nonce = payload["nonce"] | "";
    const uint64_t ts = payload["ts"] | 0ULL;
    if (nonce.isEmpty() || ts == 0) {
      setMessageScreen("Gateway Auth", "Bad challenge", "Retrying");
      refreshDisplay();
      disconnectGateway("invalid challenge");
      scheduleReconnect(true);
      return;
    }

    connectNonce = nonce;
    connectChallengeTsMs = ts;
    connectChallengeReceivedAtMs = millis();
    sendConnectRequest();
    return;
  }

  lastGatewayTickMs = millis();

  if (eventName == "tick") {
    return;
  }

  if (eventName == "node.invoke.request") {
    handleNodeInvoke(root["payload"]);
  }
}

void handleGatewayTextMessage(const uint8_t *payload) {
  JsonDocument doc;
  const DeserializationError error =
      deserializeJson(doc, reinterpret_cast<const char *>(payload));
  if (error) {
    Serial.printf("[GW] JSON parse failed: %s\n", error.c_str());
    return;
  }

  const String frameType = doc["type"] | "";
  if (frameType == "event") {
    handleGatewayEvent(doc.as<JsonVariantConst>());
  } else if (frameType == "res") {
    handleGatewayResponse(doc.as<JsonVariantConst>());
  }
}

void handleWebSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  (void)length;

  switch (type) {
    case WStype_DISCONNECTED:
      Serial.println("[GW] transport disconnected");
      gatewayTransportConnected = false;
      gatewayAuthenticated = false;
      connectRequestPending = false;
      waitingForConnectChallenge = false;
      if (pairingRequired) {
        setMessageScreen("Pairing Required", "Approve in gateway",
                         deviceId.substring(0, 12));
      } else {
        setStatusScreen(DEVICE_DISPLAY_NAME, wifiConnected ? "WiFi ready"
                                                           : "Waiting for WiFi",
                        "Gateway offline");
      }
      refreshDisplay();
      scheduleReconnect(pairingRequired);
      break;

    case WStype_CONNECTED:
      Serial.println("[GW] transport connected, awaiting challenge");
      gatewayTransportConnected = true;
      gatewayAuthenticated = false;
      waitingForConnectChallenge = true;
      connectRequestPending = false;
      connectChallengeDeadlineMs = millis() + kConnectChallengeTimeoutMs;
      setStatusScreen("Gateway", "Socket open", "Waiting challenge");
      refreshDisplay();
      break;

    case WStype_TEXT:
      handleGatewayTextMessage(payload);
      break;

    case WStype_ERROR:
      Serial.println("[GW] websocket error");
      break;

    default:
      break;
  }
}

void checkButton() {
  const bool currentState = digitalRead(PIN_BUTTON);
  if (currentState == lastButtonState) {
    return;
  }

  lastButtonState = currentState;
  const bool pressed = currentState == LOW;
  if (pressed) {
    playTone(900, 50);
  }
  handleButtonEvent(pressed);
}

class BleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
    (void)server;
    (void)connInfo;
    bleClientConnected = true;
    Serial.println("[BLE] Client connected");
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo,
                    int reason) override {
    (void)server;
    (void)connInfo;
    (void)reason;
    bleClientConnected = false;
    Serial.println("[BLE] Client disconnected");
    NimBLEDevice::startAdvertising();
  }
};

class BleCommandCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic,
               NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    const std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }
    handleBleCommand(String(value.c_str()));
  }
};

void setupBle() {
  NimBLEDevice::init(kBleDeviceName);
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new BleServerCallbacks());

  NimBLEService *service = bleServer->createService(kBleServiceUuid);

  bleCommandCharacteristic = service->createCharacteristic(
      kBleCommandCharacteristicUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::WRITE_NR);
  bleCommandCharacteristic->setCallbacks(new BleCommandCallbacks());

  bleStatusCharacteristic = service->createCharacteristic(
      kBleStatusCharacteristicUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kBleServiceUuid);
  advertising->start();
  Serial.println("[BLE] Advertising started");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println();
  Serial.println("=== OpenClaw ESP32-C3 Node ===");

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_ADC, INPUT);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  prefs.begin(kPrefsNamespace, false);

  if (sodium_init() < 0) {
    Serial.println("[AUTH] libsodium init failed");
  }

  setStatusScreen(DEVICE_DISPLAY_NAME, "Booting...", "");
  setupDisplay();
  refreshDisplay();

  if (!loadOrCreateDeviceIdentity()) {
    setMessageScreen("Device Auth", "Identity failed", "Check serial log");
    refreshDisplay();
  }

  loadSettings();
  setupBle();

  webSocket.onEvent(handleWebSocketEvent);

  playTone(1200, 60);

  if (connectWiFi()) {
    nextReconnectAttemptMs = millis();
  } else {
    nextReconnectAttemptMs = millis() + kReconnectInitialMs;
  }
}

void loop() {
  webSocket.loop();

  const unsigned long now = millis();

  if (now - lastButtonPollMs >= DEBOUNCE_MS) {
    checkButton();
    lastButtonPollMs = now;
  }

  if (displayMode == DisplayMode::Otp && otpCode[0] != '\0' &&
      now > otpExpiryMs) {
    clearOtpCode();
  }

  if (now - lastDisplayRefreshMs >= kDisplayRefreshMs) {
    refreshDisplay();
    lastDisplayRefreshMs = now;
  }

  if (waitingForConnectChallenge && now > connectChallengeDeadlineMs) {
    setMessageScreen("Gateway Timeout", "No challenge", "Retrying");
    refreshDisplay();
    disconnectGateway("challenge timeout");
    scheduleReconnect(true);
  }

  if (gatewayAuthenticated && lastGatewayTickMs > 0 &&
      now - lastGatewayTickMs > gatewayTickIntervalMs * 2UL) {
    setMessageScreen("Gateway Timeout", "Missed ticks", "Reconnecting");
    refreshDisplay();
    disconnectGateway("tick timeout");
    scheduleReconnect(true);
  }

  if (!wifiConnected && WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
  } else if (wifiConnected && WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    disconnectGateway("wifi dropped");
    scheduleReconnect(true);
  }

  if (!wifiConnected && !wifiSsid.isEmpty()) {
    static unsigned long nextWifiRetryAtMs = 0;
    if (now >= nextWifiRetryAtMs) {
      connectWiFi();
      nextWifiRetryAtMs = now + 10000;
    }
  }

  if (wifiConnected && !gatewayTransportConnected &&
      now >= nextReconnectAttemptMs && !connectRequestPending &&
      !waitingForConnectChallenge) {
    beginGatewayConnect();
  }
}
