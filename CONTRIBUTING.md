# Contributing to OpenClaw ESP32-C3 XIAO Node

Thank you for your interest in contributing! This project aims to make OpenClaw accessible on low-cost embedded hardware.

## Code of Conduct

Be respectful, inclusive, and constructive. We're all here to build something cool together.

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- Seeed XIAO ESP32-C3 + Expansion Board
- Basic understanding of C++ and embedded systems

### Development Setup

1. Fork and clone the repository:
   ```bash
   git clone https://github.com/YOUR_USERNAME/openclaw-esp32c3-xiao-node.git
   cd openclaw-esp32c3-xiao-node
   ```

2. Create your config file:
   ```bash
   cp src/config.h.example src/config.h
   # Edit src/config.h with your WiFi and gateway settings
   ```

3. Build and test:
   ```bash
   pio run                    # Build
   pio run --target upload    # Flash to device
   pio device monitor         # View serial output
   ```

## How to Contribute

### Reporting Bugs

- Check existing issues first
- Include firmware version, hardware revision, and steps to reproduce
- Attach serial output if relevant

### Suggesting Features

- Open an issue with the `enhancement` label
- Describe the use case and expected behavior
- Consider how it fits with the OpenClaw ecosystem

### Submitting Pull Requests

1. **Create a feature branch:**
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes:**
   - Follow the existing code style
   - Keep commits atomic and well-described
   - Update documentation if needed

3. **Test thoroughly:**
   - Ensure firmware compiles without warnings
   - Test on actual hardware if possible
   - Verify WebSocket connection to gateway

4. **Submit PR:**
   - Reference any related issues
   - Describe what changed and why
   - Include any testing notes

## Code Style

### General Guidelines

- Use descriptive variable and function names
- Comment complex logic, not obvious code
- Keep functions focused and reasonably sized
- Prefer `const` where possible

### Formatting

- 4-space indentation
- Opening braces on same line
- Max line length: 100 characters
- Use clang-format for consistency:
  ```bash
  clang-format -i src/*.cpp src/*.h
  ```

### Example

```cpp
// Good
void handleCommand(JsonDocument& doc) {
    const char* type = doc["type"];
    String requestId = doc["id"] | "";
    
    if (strcmp(type, "node.invoke") == 0) {
        processInvoke(doc, requestId);
    }
}

// Avoid
void hc(JsonDocument& d) {
    // unclear names, no comments
    if(strcmp(d["type"],"node.invoke")==0){pi(d,d["id"]);}
}
```

## Architecture

### Key Components

| File | Purpose |
|------|---------|
| `main.cpp` | Entry point, setup, main loop |
| `config.h` | User configuration (gitignored) |
| `config.h.example` | Configuration template |

### Adding New Commands

1. Add capability to `sendCapabilities()`:
   ```cpp
   caps.add("mymodule.mycommand");
   ```

2. Add handler in `handleCommand()`:
   ```cpp
   else if (strcmp(command, "mymodule.mycommand") == 0) {
       cmdMyModuleMyCommand(params, requestId);
   }
   ```

3. Implement the command function:
   ```cpp
   void cmdMyModuleMyCommand(JsonDocument& params, String& requestId) {
       // Your implementation
       
       // Send response
       JsonDocument resp;
       resp["type"] = "node.response";
       resp["id"] = requestId;
       resp["ok"] = true;
       String json;
       serializeJson(resp, json);
       webSocket.sendTXT(json);
   }
   ```

### Adding New Events

```cpp
void sendMyEvent() {
    if (!wsConnected) return;
    
    JsonDocument doc;
    doc["type"] = "node.event";
    doc["event"] = "mymodule.myevent";
    doc["data"] = "your data here";
    doc["timestamp"] = millis();
    
    String json;
    serializeJson(doc, json);
    webSocket.sendTXT(json);
}
```

## Testing

### Manual Testing Checklist

- [ ] Firmware compiles without errors or warnings
- [ ] Device connects to WiFi
- [ ] WebSocket connects to gateway
- [ ] OLED displays status correctly
- [ ] Button events are sent to gateway
- [ ] Buzzer responds to commands
- [ ] Heartbeat is sent periodically
- [ ] Device reconnects after WiFi/gateway drop

### Simulating Gateway

For testing without a full OpenClaw gateway:

```python
# Simple WebSocket echo server
import asyncio
import websockets
import json

async def handler(websocket):
    async for message in websocket:
        print(f"Received: {message}")
        data = json.loads(message)
        if data.get("type") == "node.connect":
            print("Node connected!")

asyncio.run(websockets.serve(handler, "0.0.0.0", 18789))
```

## Release Process

1. Update version in code if applicable
2. Create annotated tag: `git tag -a v1.0.0 -m "Release v1.0.0"`
3. Push tag: `git push origin v1.0.0`
4. GitHub Actions will build and create the release

## Questions?

- Open a GitHub issue
- Join the [OpenClaw Discord](https://discord.com/invite/clawd)
- Check [OpenClaw docs](https://docs.openclaw.ai)

---

Thank you for contributing! 🎉
