/*
 * OpenClaw ESP32-C3 Node Firmware
 * For: Seeed XIAO ESP32-C3 + Expansion Board
 * 
 * Connects to OpenClaw Gateway as a peripheral node
 * Exposes: display, button, buzzer, RTC, sensors
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
// #include <PCF8563.h>  // Will add proper RTC later
#include "config.h"

// ==== Global Objects ====
WebSocketsClient webSocket;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL, PIN_I2C_SDA);
// PCF8563 rtc;  // Disabled until proper lib found
bool rtcAvailable = false;

// ==== State ====
bool wsConnected = false;
bool wifiConnected = false;
unsigned long lastHeartbeat = 0;
unsigned long lastButtonCheck = 0;
bool lastButtonState = HIGH;
String deviceId = "";
String deviceToken = "";

// ==== Forward Declarations ====
void setupWiFi();
void setupDisplay();
void setupWebSocket();
void handleWebSocketEvent(WStype_t type, uint8_t* payload, size_t length);
void handleCommand(JsonDocument& doc);
void sendCapabilities();
void sendHeartbeat();
void checkButton();
void displayStatus(const char* line1, const char* line2 = "", const char* line3 = "");
void beep(int frequency, int duration);

// ==== Command Handlers ====
void cmdCanvasShow(JsonDocument& params, String& requestId);
void cmdCanvasClear(JsonDocument& params, String& requestId);
void cmdCanvasQR(JsonDocument& params, String& requestId);
void cmdAudioBeep(JsonDocument& params, String& requestId);
void cmdAudioTone(JsonDocument& params, String& requestId);
void cmdTimeGet(JsonDocument& params, String& requestId);
void cmdTimeSet(JsonDocument& params, String& requestId);
void cmdSystemInfo(JsonDocument& params, String& requestId);

// ==== Setup ====
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== OpenClaw ESP32-C3 Node ===");
    
    // Initialize pins
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(PIN_BUZZER, OUTPUT);
    
    // Initialize I2C
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    
    // Initialize display
    setupDisplay();
    displayStatus("OpenClaw Node", "Initializing...");
    
    // Initialize RTC (disabled for now)
    // rtc.init();
    rtcAvailable = false;
    
    // Boot beep
    beep(1000, 100);
    
    // Connect WiFi
    setupWiFi();
    
    // Connect to Gateway
    setupWebSocket();
}

// ==== Main Loop ====
void loop() {
    webSocket.loop();
    
    // Heartbeat
    if (wsConnected && (millis() - lastHeartbeat > HEARTBEAT_INTERVAL_MS)) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }
    
    // Button check
    if (millis() - lastButtonCheck > DEBOUNCE_MS) {
        checkButton();
        lastButtonCheck = millis();
    }
}

// ==== WiFi Setup ====
void setupWiFi() {
    Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
    displayStatus("WiFi", "Connecting...", WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        displayStatus("WiFi Connected", WiFi.localIP().toString().c_str());
        beep(1500, 100);
    } else {
        Serial.println("\nWiFi connection failed!");
        displayStatus("WiFi FAILED", "Check config");
        beep(500, 500);
    }
}

// ==== Display Setup ====
void setupDisplay() {
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.clearBuffer();
    u8g2.sendBuffer();
}

// ==== WebSocket Setup ====
void setupWebSocket() {
    if (!wifiConnected) return;
    
    Serial.printf("Connecting to Gateway: %s:%d\n", GATEWAY_HOST, GATEWAY_PORT);
    displayStatus("Gateway", "Connecting...", GATEWAY_HOST);
    
    webSocket.begin(GATEWAY_HOST, GATEWAY_PORT, "/");
    webSocket.onEvent(handleWebSocketEvent);
    webSocket.setReconnectInterval(RECONNECT_DELAY_MS);
}

// ==== WebSocket Event Handler ====
void handleWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            Serial.println("WebSocket disconnected");
            wsConnected = false;
            displayStatus("Gateway", "Disconnected");
            break;
            
        case WStype_CONNECTED:
            Serial.println("WebSocket connected!");
            wsConnected = true;
            displayStatus("Gateway", "Connected!", "Registering...");
            beep(2000, 100);
            sendCapabilities();
            break;
            
        case WStype_TEXT: {
            Serial.printf("Received: %s\n", payload);
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);
            if (!error) {
                handleCommand(doc);
            }
            break;
        }
            
        case WStype_ERROR:
            Serial.println("WebSocket error");
            break;
            
        default:
            break;
    }
}

// ==== Send Capabilities ====
void sendCapabilities() {
    JsonDocument doc;
    doc["type"] = "node.connect";
    doc["role"] = "node";
    
    JsonObject device = doc["device"].to<JsonObject>();
    device["name"] = DEVICE_NAME;
    device["type"] = DEVICE_TYPE;
    device["mac"] = WiFi.macAddress();
    device["ip"] = WiFi.localIP().toString();
    
    JsonArray caps = doc["capabilities"].to<JsonArray>();
    caps.add("canvas.show");
    caps.add("canvas.clear");
    caps.add("canvas.qr");
    caps.add("audio.beep");
    caps.add("audio.tone");
    caps.add("time.get");
    caps.add("time.set");
    caps.add("system.info");
    caps.add("input.button");
    
    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(json);
    Serial.println("Sent capabilities");
}

// ==== Send Heartbeat ====
void sendHeartbeat() {
    JsonDocument doc;
    doc["type"] = "node.heartbeat";
    doc["uptime"] = millis();
    doc["rssi"] = WiFi.RSSI();
    doc["freeHeap"] = ESP.getFreeHeap();
    
    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(json);
}

// ==== Handle Incoming Command ====
void handleCommand(JsonDocument& doc) {
    const char* type = doc["type"];
    String requestId = doc["id"] | "";
    
    if (strcmp(type, "node.invoke") == 0) {
        const char* command = doc["command"];
        JsonDocument params = doc["params"];
        
        Serial.printf("Command: %s\n", command);
        
        if (strcmp(command, "canvas.show") == 0) cmdCanvasShow(params, requestId);
        else if (strcmp(command, "canvas.clear") == 0) cmdCanvasClear(params, requestId);
        else if (strcmp(command, "audio.beep") == 0) cmdAudioBeep(params, requestId);
        else if (strcmp(command, "audio.tone") == 0) cmdAudioTone(params, requestId);
        else if (strcmp(command, "time.get") == 0) cmdTimeGet(params, requestId);
        else if (strcmp(command, "time.set") == 0) cmdTimeSet(params, requestId);
        else if (strcmp(command, "system.info") == 0) cmdSystemInfo(params, requestId);
    }
}

// ==== Button Check ====
void checkButton() {
    bool currentState = digitalRead(PIN_BUTTON);
    
    if (currentState != lastButtonState) {
        lastButtonState = currentState;
        
        if (wsConnected) {
            JsonDocument doc;
            doc["type"] = "node.event";
            doc["event"] = "input.button";
            doc["state"] = currentState == LOW ? "pressed" : "released";
            doc["timestamp"] = millis();
            
            String json;
            serializeJson(doc, json);
            webSocket.sendTXT(json);
            
            Serial.printf("Button %s\n", currentState == LOW ? "pressed" : "released");
        }
        
        if (currentState == LOW) {
            beep(800, 50);
        }
    }
}

// ==== Display Helper ====
void displayStatus(const char* line1, const char* line2, const char* line3) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 12, line1);
    u8g2.drawStr(0, 28, line2);
    u8g2.drawStr(0, 44, line3);
    
    // Status bar
    u8g2.drawLine(0, 54, 128, 54);
    if (wifiConnected) {
        u8g2.drawStr(0, 64, "WiFi:OK");
    } else {
        u8g2.drawStr(0, 64, "WiFi:--");
    }
    if (wsConnected) {
        u8g2.drawStr(60, 64, "GW:OK");
    } else {
        u8g2.drawStr(60, 64, "GW:--");
    }
    
    u8g2.sendBuffer();
}

// ==== Buzzer Helper ====
void beep(int frequency, int duration) {
    tone(PIN_BUZZER, frequency, duration);
}

// ==== Command: canvas.show ====
void cmdCanvasShow(JsonDocument& params, String& requestId) {
    const char* text = params["text"] | "";
    int x = params["x"] | 0;
    int y = params["y"] | 12;
    int size = params["size"] | 1;
    bool clear = params["clear"] | true;
    
    if (clear) {
        u8g2.clearBuffer();
    }
    
    if (size == 2) {
        u8g2.setFont(u8g2_font_10x20_tf);
    } else if (size == 3) {
        u8g2.setFont(u8g2_font_logisoso16_tf);
    } else {
        u8g2.setFont(u8g2_font_6x10_tf);
    }
    
    u8g2.drawStr(x, y, text);
    u8g2.sendBuffer();
    
    // Send response
    JsonDocument resp;
    resp["type"] = "node.response";
    resp["id"] = requestId;
    resp["ok"] = true;
    String json;
    serializeJson(resp, json);
    webSocket.sendTXT(json);
}

// ==== Command: canvas.clear ====
void cmdCanvasClear(JsonDocument& params, String& requestId) {
    u8g2.clearBuffer();
    u8g2.sendBuffer();
    
    JsonDocument resp;
    resp["type"] = "node.response";
    resp["id"] = requestId;
    resp["ok"] = true;
    String json;
    serializeJson(resp, json);
    webSocket.sendTXT(json);
}

// ==== Command: audio.beep ====
void cmdAudioBeep(JsonDocument& params, String& requestId) {
    int frequency = params["frequency"] | 1000;
    int duration = params["duration"] | 200;
    
    beep(frequency, duration);
    
    JsonDocument resp;
    resp["type"] = "node.response";
    resp["id"] = requestId;
    resp["ok"] = true;
    String json;
    serializeJson(resp, json);
    webSocket.sendTXT(json);
}

// ==== Command: audio.tone ====
void cmdAudioTone(JsonDocument& params, String& requestId) {
    JsonArray melody = params["melody"].as<JsonArray>();
    
    for (JsonVariant note : melody) {
        int freq = note["f"] | 1000;
        int dur = note["d"] | 100;
        beep(freq, dur);
        delay(dur + 50);
    }
    
    JsonDocument resp;
    resp["type"] = "node.response";
    resp["id"] = requestId;
    resp["ok"] = true;
    String json;
    serializeJson(resp, json);
    webSocket.sendTXT(json);
}

// ==== Command: time.get ====
void cmdTimeGet(JsonDocument& params, String& requestId) {
    JsonDocument resp;
    resp["type"] = "node.response";
    resp["id"] = requestId;
    
    if (!rtcAvailable) {
        resp["ok"] = false;
        resp["error"] = "RTC not available";
    } else {
        resp["ok"] = true;
        resp["message"] = "RTC support coming soon";
    }
    
    String json;
    serializeJson(resp, json);
    webSocket.sendTXT(json);
}

// ==== Command: time.set ====
void cmdTimeSet(JsonDocument& params, String& requestId) {
    JsonDocument resp;
    resp["type"] = "node.response";
    resp["id"] = requestId;
    
    if (!rtcAvailable) {
        resp["ok"] = false;
        resp["error"] = "RTC not available";
    } else {
        resp["ok"] = true;
    }
    
    String json;
    serializeJson(resp, json);
    webSocket.sendTXT(json);
}

// ==== Command: system.info ====
void cmdSystemInfo(JsonDocument& params, String& requestId) {
    JsonDocument resp;
    resp["type"] = "node.response";
    resp["id"] = requestId;
    resp["ok"] = true;
    resp["chip"] = "ESP32-C3";
    resp["mac"] = WiFi.macAddress();
    resp["ip"] = WiFi.localIP().toString();
    resp["rssi"] = WiFi.RSSI();
    resp["freeHeap"] = ESP.getFreeHeap();
    resp["uptime"] = millis();
    resp["cpuFreq"] = ESP.getCpuFreqMHz();
    resp["flashSize"] = ESP.getFlashChipSize();
    resp["sdkVersion"] = ESP.getSdkVersion();
    
    String json;
    serializeJson(resp, json);
    webSocket.sendTXT(json);
}
