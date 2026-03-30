#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "esp32-hal-ledc.h"

namespace {
constexpr int kButtonPin = D1;
constexpr int kSpeakerPin = D3;
constexpr int kI2cSdaPin = D4;
constexpr int kI2cSclPin = D5;
constexpr uint8_t kSpeakerChannel = 0;
constexpr uint8_t kVisionAddress = 0x62;
constexpr uint8_t kOledAddress = 0x3C;
constexpr uint8_t kRtcAddress = 0x51;

bool lastButtonState = HIGH;
bool oledDetected = false;
bool rtcDetected = false;
bool visionDetected = false;
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
    U8G2_R0, kI2cSclPin, kI2cSdaPin, U8X8_PIN_NONE);

bool probeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void printProbeResult(const char *label, uint8_t address) {
  Serial.printf("[%s] 0x%02X %s\n", label, address,
                probeAddress(address) ? "DETECTED" : "missing");
}

void scanI2CBus() {
  Serial.println("Scanning I2C bus...");
  int found = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    if (!probeAddress(address)) {
      continue;
    }
    Serial.printf("  Found device at 0x%02X\n", address);
    ++found;
  }
  if (found == 0) {
    Serial.println("  No I2C devices detected.");
  }
  oledDetected = probeAddress(kOledAddress);
  rtcDetected = probeAddress(kRtcAddress);
  visionDetected = probeAddress(kVisionAddress);
  printProbeResult("OLED", kOledAddress);
  printProbeResult("RTC", kRtcAddress);
  printProbeResult("Grove Vision AI V2", kVisionAddress);
}

void showDisplayStatus() {
  if (!oledDetected) {
    return;
  }

  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 12, "HW Smoke Test");
  u8g2.drawStr(0, 26, rtcDetected ? "RTC: OK" : "RTC: missing");
  u8g2.drawStr(0, 40, visionDetected ? "Vision: OK" : "Vision: missing");
  u8g2.drawStr(0, 54, "Btn: press to beep");
  u8g2.sendBuffer();
}

void playSpeakerTest() {
  Serial.printf("Speaker test on pin %d\n", kSpeakerPin);
  ledcSetup(kSpeakerChannel, 1000, 10);
  ledcAttachPin(kSpeakerPin, kSpeakerChannel);
  const int notes[][2] = {
      {523, 180},
      {659, 180},
      {784, 240},
  };

  for (const auto &note : notes) {
    ledcWriteTone(kSpeakerChannel, note[0]);
    delay(note[1]);
    ledcWriteTone(kSpeakerChannel, 0);
    delay(80);
  }
  ledcDetachPin(kSpeakerPin);
  Serial.println("Speaker test complete. You should have heard three tones.");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println();
  Serial.println("=== XIAO ESP32-C3 Hardware Smoke Test ===");
  Serial.printf("CPU: %u MHz, flash: %u bytes\n", ESP.getCpuFreqMHz(),
                ESP.getFlashChipSize());

  pinMode(kButtonPin, INPUT_PULLUP);
  pinMode(kSpeakerPin, OUTPUT);
  Wire.begin(kI2cSdaPin, kI2cSclPin);

  scanI2CBus();
  showDisplayStatus();
  playSpeakerTest();

  Serial.println("Press the button to verify digital input events.");
}

void loop() {
  const bool currentState = digitalRead(kButtonPin);
  if (currentState != lastButtonState) {
    lastButtonState = currentState;
    Serial.printf("Button %s\n", currentState == LOW ? "PRESSED" : "RELEASED");
    if (currentState == LOW) {
      ledcSetup(kSpeakerChannel, 880, 10);
      ledcAttachPin(kSpeakerPin, kSpeakerChannel);
      ledcWriteTone(kSpeakerChannel, 880);
      delay(60);
      ledcWriteTone(kSpeakerChannel, 0);
      ledcDetachPin(kSpeakerPin);
    }
    delay(30);
  }
}
