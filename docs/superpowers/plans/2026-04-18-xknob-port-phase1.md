# X-Knob Port Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the claude-desktop-buddy firmware alive on X-Knob hardware (ESP32-S3 + 240×240 GC9A01 round LCD + MT6701 rotary + single button + 8× NeoPixel + BLDC motor). MVP = BLE pairing with Claude Desktop works, one ASCII character renders, prompts can be approved / denied via the knob.

**Architecture:** Fork `claude-desktop-buddy` into a new repo `claude-desktop-buddy-xknob`. Directly replace all `M5.*` calls with new `hw_*` modules that drive X-Knob peripherals. Keep BLE protocol, NVS schema, character pack format, and `buddies/*.cpp` registry byte-identical so Claude Desktop zero-changes and so future upstream characters cherry-pick cleanly.

**Tech Stack:**
- PlatformIO, `espressif32` platform, `board = esp32-s3-devkitc-1`, Arduino framework
- TFT_eSPI (GC9A01 driver) + TFT_eSprite (240×240 16-bit, PSRAM)
- NimBLE via `ble_bridge.cpp` (ported verbatim from upstream)
- LittleFS (character packs)
- Adafruit_NeoPixel (RGB feedback)
- AnimatedGIF (character GIF decoding, upstream lib)
- ArduinoJson (BLE payload parsing, upstream)

**Upstream reference paths** (for engineers porting files verbatim):
- Buddy source: `/Users/zinklu/code/opensources/claude-desktop-buddy/src/`
- X-Knob reference: `/Users/zinklu/code/opensources/X-Knob/1.Firmware/`
- Spec: `/Users/zinklu/code/opensources/claude-desktop-buddy/docs/superpowers/specs/2026-04-18-xknob-port-design.md`

---

## File Structure

New repo `~/code/opensources/claude-desktop-buddy-xknob/`:

```
claude-desktop-buddy-xknob/
├── platformio.ini                 # NEW — ESP32-S3 DevKitC-1 env with PSRAM enabled
├── partitions.csv                 # NEW — 16 MB flash, ~8 MB LittleFS
├── .gitignore                     # NEW — .pio/, .vscode/
├── README.md                      # NEW — brief "this is a fork" note + hardware pins
│
├── src/
│   ├── main.cpp                   # NEW — rewritten, Phase 1 skeleton
│   │
│   ├── ble_bridge.h               # COPY verbatim from upstream
│   ├── ble_bridge.cpp             # COPY verbatim from upstream
│   ├── xfer.h                     # COPY verbatim from upstream
│   ├── data.h                     # COPY verbatim from upstream
│   ├── stats.h                    # COPY verbatim from upstream
│   ├── character.h                # COPY verbatim from upstream
│   ├── character.cpp              # COPY verbatim + swap sprite ref
│   ├── buddy.h                    # COPY verbatim (keep interface)
│   ├── buddy_common.h             # COPY verbatim
│   ├── buddy.cpp                  # COPY + change coord constants + swap sprite extern
│   ├── buddies/                   # COPY verbatim (18 species files unchanged)
│   │
│   ├── hw_display.h               # NEW — GC9A01 init, brightness PWM, sprite accessor
│   ├── hw_display.cpp             # NEW
│   ├── hw_input.h                 # NEW — InputEvent enum + poll()
│   ├── hw_input.cpp               # NEW — MT6701 encoder + button FSM
│   ├── hw_motor.h                 # NEW — open-loop bump
│   ├── hw_motor.cpp               # NEW
│   ├── hw_leds.h                  # NEW — NeoPixel mode enum + tick
│   ├── hw_leds.cpp                # NEW
│   ├── hw_power.h                 # NEW — ON_OFF pin hold, time() wrapper
│   └── hw_power.cpp               # NEW
│
├── test/
│   └── native/
│       ├── test_input_fsm.cpp     # NEW — host-side button FSM unit tests
│       └── test_encoder.cpp       # NEW — host-side encoder delta tests
│
├── tools/                         # COPY verbatim from upstream (python pack tools)
└── characters/                    # COPY verbatim (bundled demo characters if any)
```

**Files deliberately NOT ported from upstream** (Phase 1 scope):
- Any code referencing `M5.*`, `Axp`, `BM8563`, or `MPU6886` — replaced by `hw_*`.

---

## Pre-flight

- [ ] **Check toolchain**

Run:
```bash
pio --version
```
Expected: PlatformIO Core `>= 6.1`. If missing, install with `pip install -U platformio`.

- [ ] **Check hardware is connected**

Run:
```bash
pio device list
```
Expected: one entry for X-Knob's USB CDC (likely `/dev/cu.usbmodem*` on macOS or `/dev/ttyACM*` on Linux). Note the port — you'll use it in subsequent `pio run -t upload --upload-port <port>` commands if auto-detect fails.

---

## Task 0: Bootstrap empty repo

**Files:**
- Create: `~/code/opensources/claude-desktop-buddy-xknob/` (new directory)
- Create: `~/code/opensources/claude-desktop-buddy-xknob/.gitignore`
- Create: `~/code/opensources/claude-desktop-buddy-xknob/platformio.ini`
- Create: `~/code/opensources/claude-desktop-buddy-xknob/partitions.csv`
- Create: `~/code/opensources/claude-desktop-buddy-xknob/src/main.cpp`
- Create: `~/code/opensources/claude-desktop-buddy-xknob/README.md`

- [ ] **Step 0.1: Initialize repo**

```bash
cd ~/code/opensources
mkdir claude-desktop-buddy-xknob
cd claude-desktop-buddy-xknob
git init -b main
```

- [ ] **Step 0.2: Write `.gitignore`**

`.gitignore`:
```
.pio/
.vscode/
.DS_Store
*.swp
```

- [ ] **Step 0.3: Write `partitions.csv`** (16 MB flash, ~8 MB LittleFS, no OTA)

`partitions.csv`:
```
# Name,   Type, SubType,  Offset,   Size,     Flags
nvs,      data, nvs,      0x9000,   0x5000,
otadata,  data, ota,      0xe000,   0x2000,
app0,     app,  ota_0,    0x10000,  0x700000,
spiffs,   data, spiffs,   0x710000, 0x8E0000,
coredump, data, coredump, 0xFF0000, 0x10000,
```
(Arduino's LittleFS uses the `spiffs` partition type entry; 0x8E0000 ≈ 9 MB.)

- [ ] **Step 0.4: Write `platformio.ini`**

`platformio.ini`:
```ini
[env:x-knob]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600

board_build.filesystem = littlefs
board_build.partitions = partitions.csv
board_build.arduino.memory_type = qio_opi
board_upload.flash_size = 16MB
board_upload.maximum_size = 7340032

build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    ; TFT_eSPI configuration for X-Knob GC9A01
    -DUSER_SETUP_LOADED=1
    -DGC9A01_DRIVER=1
    -DTFT_MISO=-1
    -DTFT_MOSI=11
    -DTFT_SCLK=12
    -DTFT_CS=10
    -DTFT_DC=14
    -DTFT_RST=9
    -DTFT_BL=-1
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=240
    -DSPI_FREQUENCY=40000000
    -DLOAD_GLCD=1
    -DLOAD_FONT2=1
    -DLOAD_FONT4=1
    -DSMOOTH_FONT=1

lib_deps =
    bodmer/TFT_eSPI@^2.5.43
    adafruit/Adafruit NeoPixel@^1.12.0
    bitbank2/AnimatedGIF@^2.1.1
    bblanchon/ArduinoJson@^7.0.0
    h2zero/NimBLE-Arduino@^1.4.1
    SPI
    Wire
```

- [ ] **Step 0.5: Write minimal `src/main.cpp`**

`src/main.cpp`:
```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot OK");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    Serial.printf("alive %lus\n", last / 1000);
  }
}
```

- [ ] **Step 0.6: Write `README.md`**

`README.md`:
```markdown
# claude-desktop-buddy-xknob

Fork of [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) targeting [X-Knob](https://github.com/Makerfabs/Project_X-Knob) hardware (ESP32-S3 + 240×240 round LCD + rotary knob).

Phase 1 scope: BLE pairing, one ASCII character, approve/deny prompts. See `docs/` (copy from upstream spec) for full design.

## Build

```
pio run -t upload
pio device monitor
```

Pin map and hardware details are in `platformio.ini` `build_flags`.
```

- [ ] **Step 0.7: First build**

```bash
cd ~/code/opensources/claude-desktop-buddy-xknob
pio run
```
Expected: `SUCCESS`. Elf links. No warnings about missing partitions or board.

If "board not found": update PlatformIO espressif32 platform with `pio pkg update`.

- [ ] **Step 0.8: Flash and verify serial**

```bash
pio run -t upload
pio device monitor
```
Expected: see `xknob-buddy: boot OK`, then `alive 1s`, `alive 2s`, … at 1 Hz.

If no output: press reset on the X-Knob board; check USB cable is a data cable not power-only; confirm `pio device list` shows the port.

- [ ] **Step 0.9: Commit**

```bash
git add .
git commit -m "bootstrap: empty pio project flashes and prints alive"
```

---

## Task 1: Port BLE bridge and support code

Goal: get BLE pairing working before we even touch the screen. This validates toolchain on the BLE-heavy upstream code (NimBLE, LittleFS, ArduinoJson) and lets us verify pairing with Claude Desktop via serial.

**Files:**
- Create by copying verbatim from `/Users/zinklu/code/opensources/claude-desktop-buddy/src/`:
  - `src/ble_bridge.h`, `src/ble_bridge.cpp`
  - `src/xfer.h`
  - `src/data.h`
  - `src/stats.h`
- Modify: `src/main.cpp` (add `bleInit` call)
- Create: `src/stats.cpp` (stub — stats persistence is NVS-only, no hardware deps — see step 1.3)

- [ ] **Step 1.1: Copy BLE bridge and data headers verbatim**

```bash
cd ~/code/opensources/claude-desktop-buddy-xknob/src
cp ~/code/opensources/claude-desktop-buddy/src/ble_bridge.h .
cp ~/code/opensources/claude-desktop-buddy/src/ble_bridge.cpp .
cp ~/code/opensources/claude-desktop-buddy/src/xfer.h .
cp ~/code/opensources/claude-desktop-buddy/src/data.h .
cp ~/code/opensources/claude-desktop-buddy/src/stats.h .
```

- [ ] **Step 1.2: Inspect `ble_bridge.cpp` for M5 dependencies**

Run:
```bash
grep -n 'M5\|Axp\|BM8563\|MPU' src/ble_bridge.cpp
```
Expected: no matches (upstream `ble_bridge.cpp` is hardware-agnostic). If any match, either:
- The symbol is in a comment → leave it
- The symbol is called → escalate to user before proceeding

- [ ] **Step 1.3: Check if upstream ships `stats.cpp` separately**

Run:
```bash
ls /Users/zinklu/code/opensources/claude-desktop-buddy/src/stats*
```
If `stats.cpp` exists upstream: copy it verbatim.
```bash
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/stats.cpp src/ 2>/dev/null || true
```
If no `stats.cpp` upstream (the project inlines the implementation into `main.cpp`): leave `stats.h` alone for now; we'll handle stats logic in Task 7 when we port support code.

Verify with grep:
```bash
grep -n 'statsLoad\|statsSave\|statsPollLevelUp' /Users/zinklu/code/opensources/claude-desktop-buddy/src/main.cpp | head
```
If matches are found only in `main.cpp`, stats is inlined; defer to Task 7.

- [ ] **Step 1.4: Check if upstream ships `data.cpp`**

Run:
```bash
ls /Users/zinklu/code/opensources/claude-desktop-buddy/src/data.*
```
If `data.cpp` exists: copy it. Otherwise the data parsing is inlined in `main.cpp` and we'll move it in Task 7.

- [ ] **Step 1.5: Minimally wire BLE into `main.cpp`**

Overwrite `src/main.cpp` with:
```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "ble_bridge.h"

static char btName[16] = "Claude";

static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  startBt();
  Serial.printf("advertising as %s\n", btName);
}

void loop() {
  static uint32_t lastPasskey = 0;
  uint32_t pk = blePasskey();
  if (pk && pk != lastPasskey) {
    Serial.printf("passkey: %06lu\n", (unsigned long)pk);
  }
  lastPasskey = pk;

  static bool wasConn = false;
  bool conn = bleConnected();
  if (conn != wasConn) {
    Serial.printf("ble: %s\n", conn ? "connected" : "disconnected");
    wasConn = conn;
  }

  // Drain inbound BLE, just print for now
  while (bleAvailable()) {
    int c = bleRead();
    if (c >= 0) Serial.write((char)c);
  }

  delay(20);
}
```

- [ ] **Step 1.6: Build**

```bash
pio run
```
Expected: `SUCCESS`. If link errors about undefined symbols from `data.h` / `stats.h` types: these headers declare types and functions that are implemented in `main.cpp` upstream. In our new layout, we only referenced `ble_bridge.h` in main so the other headers won't be linked yet. Good.

- [ ] **Step 1.7: Flash and verify BLE advertises**

```bash
pio run -t upload && pio device monitor
```
Expected on serial:
```
xknob-buddy: boot
advertising as Claude-XXXX
```

Expected on phone / mac BLE scanner (e.g. `nRF Connect` app, or `bluetoothctl scan on`): device named `Claude-XXXX` visible.

- [ ] **Step 1.8: Verify pairing with Claude Desktop**

In Claude Desktop → Settings → Advanced → enable Developer Mode → Developer menu → Open Hardware Buddy → Connect → pick `Claude-XXXX`. When prompted for a 6-digit code, read it from the X-Knob serial output line `passkey: NNNNNN`. Enter it in Claude Desktop.

Expected serial output after pairing:
```
passkey: 123456
ble: connected
{"cmd":"sync", ...}  (raw JSON streaming in)
```

If pairing fails: check `bleSecure()` didn't throw; check `NimBLE` logs at `CORE_DEBUG_LEVEL=3`. Usually wrong NimBLE version or PSRAM not enabled are the culprits.

- [ ] **Step 1.9: Commit**

```bash
git add .
git commit -m "ble: port bridge verbatim, pairing verified end-to-end"
```

---

## Task 2: `hw_display` — GC9A01 live with color calibration

**Files:**
- Create: `src/hw_display.h`, `src/hw_display.cpp`
- Modify: `src/main.cpp` (add display init + test pattern)

- [ ] **Step 2.1: Write `hw_display.h`**

`src/hw_display.h`:
```cpp
#pragma once
#include <TFT_eSPI.h>

void          hw_display_init();
void          hw_display_set_brightness(uint8_t pct);  // 0..100
TFT_eSPI&     hw_display_tft();
TFT_eSprite&  hw_display_sprite();
```

- [ ] **Step 2.2: Write `hw_display.cpp`**

`src/hw_display.cpp`:
```cpp
#include "hw_display.h"
#include <Arduino.h>

static TFT_eSPI _tft;
static TFT_eSprite _spr(&_tft);

// PWM on TFT_BLK. LEDC channel 0, 5 kHz, 8-bit.
static const int BLK_PIN = 13;
static const int BLK_CH  = 0;

void hw_display_init() {
  // Backlight PWM
  ledcSetup(BLK_CH, 5000, 8);
  ledcAttachPin(BLK_PIN, BLK_CH);
  hw_display_set_brightness(0);  // off while we init to hide glitches

  _tft.init();
  _tft.setRotation(0);
  _tft.fillScreen(TFT_BLACK);

  // X-Knob panel quirks: reference hal/lcd.cpp from X-Knob upstream.
  // Starting values derived from X-Knob init sequence; adjust in step 2.5 if colors are wrong.
  _tft.invertDisplay(true);

  // 240×240 16-bit sprite in PSRAM (240*240*2 = 115 KB).
  // TFT_eSPI 2.5+ picks PSRAM automatically when BOARD_HAS_PSRAM is set and
  // sprite > threshold. If it doesn't we'll handle in step 2.4.
  _spr.setColorDepth(16);
  _spr.createSprite(240, 240);
  if (_spr.getPointer() == nullptr) {
    Serial.println("hw_display: sprite alloc FAILED");
  } else {
    Serial.printf("hw_display: sprite at %p, psram free %u KB\n",
                  _spr.getPointer(), (unsigned)(ESP.getFreePsram() / 1024));
  }

  hw_display_set_brightness(50);
}

void hw_display_set_brightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(BLK_CH, (pct * 255) / 100);
}

TFT_eSPI&     hw_display_tft()    { return _tft; }
TFT_eSprite&  hw_display_sprite() { return _spr; }
```

- [ ] **Step 2.3: Modify `src/main.cpp` to draw a calibration pattern**

Replace `src/main.cpp` with:
```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "ble_bridge.h"
#include "hw_display.h"

static char btName[16] = "Claude";
static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

static void drawCalibration() {
  TFT_eSprite& spr = hw_display_sprite();
  spr.fillSprite(TFT_BLACK);

  // Top-left red, top-right green, bottom-left blue, bottom-right white.
  // If any quadrant's color is wrong, we fix invertDisplay / color order in step 2.5.
  spr.fillRect(0,   0,   120, 120, TFT_RED);
  spr.fillRect(120, 0,   120, 120, TFT_GREEN);
  spr.fillRect(0,   120, 120, 120, TFT_BLUE);
  spr.fillRect(120, 120, 120, 120, TFT_WHITE);

  // Centered text reading "R G / B W" to double-check orientation.
  spr.setTextColor(TFT_BLACK);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(2);
  spr.drawString("R", 60,  60);
  spr.drawString("G", 180, 60);
  spr.drawString("B", 60,  180);
  spr.drawString("W", 180, 180);

  spr.pushSprite(0, 0);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("xknob-buddy: boot");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

  hw_display_init();
  drawCalibration();

  startBt();
}

void loop() {
  delay(1000);
}
```

- [ ] **Step 2.4: Build and flash**

```bash
pio run -t upload && pio device monitor
```
Expected serial:
```
xknob-buddy: boot
hw_display: sprite at 0x3fcXXXXX, psram free NNNN KB
```
If `sprite alloc FAILED`: PSRAM is not enabled. Check `platformio.ini` has `BOARD_HAS_PSRAM` and `board_build.arduino.memory_type = qio_opi`. Re-flash with `pio run -t erase` first, then re-upload. If still failing, the X-Knob PCB may not have PSRAM — escalate to user; fallback plan is to drop to 8-bit sprite (`setColorDepth(8)`, 57 KB).

- [ ] **Step 2.5: Calibrate display orientation & color**

Look at the screen. Expected: **top-left red (with "R"), top-right green ("G"), bottom-left blue ("B"), bottom-right white ("W")**. Quadrants should occupy the circular visible area.

- If colors look wrong (e.g. red appears cyan, blue appears yellow, every color has its complement): the panel uses inverted color. Toggle `_tft.invertDisplay(false)` vs `true` in `hw_display.cpp`.
- If colors are swapped (red↔blue): color order is wrong. Add `-DTFT_RGB_ORDER=TFT_BGR` to `platformio.ini` build_flags.
- If R/G/B/W quadrants appear mirrored or rotated: adjust `_tft.setRotation(N)` where N ∈ {0, 1, 2, 3}.
- Reference `/Users/zinklu/code/opensources/X-Knob/1.Firmware/src/hal/lcd.cpp` — X-Knob's known-good init commands. Match them.

Iterate until screen looks correct. Each change = rebuild + flash.

- [ ] **Step 2.6: Verify frame rate**

Append a small FPS counter to `loop()`:
```cpp
void loop() {
  static uint32_t frames = 0, lastReport = 0;
  TFT_eSprite& spr = hw_display_sprite();
  spr.fillSprite((frames & 1) ? TFT_BLACK : TFT_WHITE);
  spr.setTextColor(TFT_RED);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(4);
  spr.drawNumber(frames, 120, 120);
  spr.pushSprite(0, 0);
  frames++;
  if (millis() - lastReport > 1000) {
    Serial.printf("fps: %lu\n", frames);
    frames = 0;
    lastReport = millis();
  }
}
```
Flash, observe serial. Expected: `fps: 30` or higher. If < 20 fps: SPI freq may be too low for the panel — try raising `-DSPI_FREQUENCY=80000000` (40→80 MHz). If that causes artifacts, stick with 40 MHz and accept the frame rate.

Once verified, revert `loop()` to the version from step 2.3.

- [ ] **Step 2.7: Commit**

```bash
git add .
git commit -m "display: GC9A01 init with 240x240 psram sprite, calibrated"
```

---

## Task 3: `hw_power` — physical power hold + time source

Short task. The MT6701 encoder, BLDC motor, and NeoPixel all need initialized hardware, and `ON_OFF_PIN` needs to be held high to keep the board powered on after the user presses and releases the physical power switch.

**Files:**
- Create: `src/hw_power.h`, `src/hw_power.cpp`
- Modify: `src/main.cpp` (call `hw_power_init` first thing)

- [ ] **Step 3.1: Write `hw_power.h`**

`src/hw_power.h`:
```cpp
#pragma once
#include <sys/time.h>

void    hw_power_init();
void    hw_power_off();       // Drive ON_OFF_PIN low to fully cut power
time_t  hw_power_now();       // Wraps time(nullptr) — valid once BLE has synced RTC
```

- [ ] **Step 3.2: Write `hw_power.cpp`**

`src/hw_power.cpp`:
```cpp
#include "hw_power.h"
#include <Arduino.h>
#include <time.h>

static const int ON_OFF_PIN      = 18;
static const int BATTERY_OFF_PIN = 7;

void hw_power_init() {
  // Hold main power on. X-Knob's physical power switch momentarily pulls this
  // high; firmware must then hold it to keep the regulator enabled after
  // release. Mirrors X-Knob upstream hal/power.cpp behavior.
  pinMode(ON_OFF_PIN, OUTPUT);
  digitalWrite(ON_OFF_PIN, HIGH);

  // Battery cut pin — keep high to not disconnect battery accidentally.
  pinMode(BATTERY_OFF_PIN, OUTPUT);
  digitalWrite(BATTERY_OFF_PIN, HIGH);
}

void hw_power_off() {
  digitalWrite(ON_OFF_PIN, LOW);
  delay(1000);
  // If we're still running, we're on USB power; fall through to a halt.
  while (true) { delay(1000); }
}

time_t hw_power_now() {
  time_t t = 0;
  time(&t);
  return t;
}
```

- [ ] **Step 3.3: Call `hw_power_init` first in `setup()`**

In `src/main.cpp`, add `#include "hw_power.h"` and make `hw_power_init()` the very first line of `setup()`:
```cpp
void setup() {
  hw_power_init();              // FIRST: hold power rail
  Serial.begin(115200);
  ...
}
```

- [ ] **Step 3.4: Build and flash**

```bash
pio run -t upload && pio device monitor
```
Expected: same output as before. The functional test is that after pressing the physical power switch briefly (if your X-Knob has one routed through firmware control), the device stays on instead of shutting off.

- [ ] **Step 3.5: Commit**

```bash
git add .
git commit -m "power: hold ON_OFF high, provide time wrapper"
```

---

## Task 4: `hw_input` — encoder + button events

This is the most logic-heavy new file. We'll use TDD: write host-side tests for the button FSM and encoder delta logic, then implement, then wire it up on the device.

**Files:**
- Create: `src/hw_input.h`, `src/hw_input.cpp`
- Create: `test/native/test_input_fsm.cpp`
- Create: `test/native/test_encoder.cpp`
- Modify: `platformio.ini` (add native test env)
- Modify: `src/main.cpp` (print events from `hw_input_poll()`)

### 4a. Native test env setup

- [ ] **Step 4.1: Add native env to `platformio.ini`**

Append to `platformio.ini`:
```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17
```

- [ ] **Step 4.2: Add interface header `hw_input.h`**

`src/hw_input.h`:
```cpp
#pragma once
#include <stdint.h>

enum InputEvent {
  EVT_NONE = 0,
  EVT_ROT_CW,
  EVT_ROT_CCW,
  EVT_CLICK,
  EVT_DOUBLE,
  EVT_LONG,
};

void       hw_input_init();
InputEvent hw_input_poll();   // Returns one pending event, or EVT_NONE

// Internal helpers, exposed for unit tests.
namespace hw_input_internal {
  // Button FSM: feed it (pressed, now_ms). Returns one of EVT_NONE / EVT_CLICK /
  // EVT_DOUBLE / EVT_LONG; stateful across calls. Thresholds:
  //   LONG_MS   = 600
  //   CLICK_MAX = 500
  //   DOUBLE_GAP_MS = 300
  struct ButtonFSM {
    bool    prevPressed    = false;
    uint32_t pressStartMs  = 0;
    uint32_t lastReleaseMs = 0;
    bool    longFired      = false;
    bool    awaitingDouble = false;
    uint32_t awaitExpireMs = 0;
  };
  InputEvent buttonStep(ButtonFSM& s, bool pressed, uint32_t now);

  // Encoder delta: tracks accumulated raw angle (degrees, float). Every time
  // accumulated angle crosses ±DETENT_DEG (15.0) from last emit, emit ROT_CW
  // or ROT_CCW. Returns EVT_NONE if no detent crossed.
  struct EncoderFSM {
    float lastEmitDeg = 0.0f;
  };
  InputEvent encoderStep(EncoderFSM& s, float currentDeg);
}
```

### 4b. Button FSM — TDD

- [ ] **Step 4.3: Write failing button FSM tests**

`test/native/test_input_fsm.cpp`:
```cpp
#include <unity.h>
#include "../../src/hw_input.h"

using namespace hw_input_internal;

void setUp() {}
void tearDown() {}

void test_no_press_no_event() {
  ButtonFSM s;
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 0));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 100));
}

void test_short_press_emits_click() {
  ButtonFSM s;
  // Press at t=0
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 0));
  // Still pressed at t=100
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 100));
  // Released at t=200 — below 500 ms click max, above 0 duration
  // First release does NOT emit immediately; we wait for DOUBLE window.
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 200));
  // After DOUBLE gap (300 ms) elapsed, emit CLICK
  TEST_ASSERT_EQUAL(EVT_CLICK, buttonStep(s, false, 501));
}

void test_long_press_emits_long_once() {
  ButtonFSM s;
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 0));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 500));
  // At 600 ms LONG fires
  TEST_ASSERT_EQUAL(EVT_LONG, buttonStep(s, true, 600));
  // Holding continues — must NOT refire
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 1000));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 2000));
  // Release should NOT emit CLICK either (LONG consumed the press)
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 2100));
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, false, 2500));
}

void test_double_click() {
  ButtonFSM s;
  // First click
  buttonStep(s, true, 0);
  buttonStep(s, false, 100);
  // Second press before double-gap
  TEST_ASSERT_EQUAL(EVT_NONE, buttonStep(s, true, 200));
  // Second release — emit DOUBLE immediately
  TEST_ASSERT_EQUAL(EVT_DOUBLE, buttonStep(s, false, 300));
}

void test_two_slow_clicks_emit_two_clicks() {
  ButtonFSM s;
  buttonStep(s, true, 0);
  buttonStep(s, false, 100);
  // DOUBLE window (300 ms) expires -> emit CLICK
  TEST_ASSERT_EQUAL(EVT_CLICK, buttonStep(s, false, 401));
  // Second click starts well after
  buttonStep(s, true, 1000);
  buttonStep(s, false, 1100);
  TEST_ASSERT_EQUAL(EVT_CLICK, buttonStep(s, false, 1401));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_press_no_event);
  RUN_TEST(test_short_press_emits_click);
  RUN_TEST(test_long_press_emits_long_once);
  RUN_TEST(test_double_click);
  RUN_TEST(test_two_slow_clicks_emit_two_clicks);
  return UNITY_END();
}
```

- [ ] **Step 4.4: Run tests — expect FAIL**

```bash
pio test -e native -f test_input_fsm
```
Expected: linker error about unresolved `buttonStep` symbol, or all tests fail because the function isn't implemented.

- [ ] **Step 4.5: Write minimal `hw_input.cpp` implementing button FSM**

`src/hw_input.cpp`:
```cpp
#include "hw_input.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <Wire.h>
#endif

namespace hw_input_internal {

static const uint32_t LONG_MS       = 600;
static const uint32_t CLICK_MAX_MS  = 500;
static const uint32_t DOUBLE_GAP_MS = 300;

InputEvent buttonStep(ButtonFSM& s, bool pressed, uint32_t now) {
  // Edge: press
  if (pressed && !s.prevPressed) {
    s.prevPressed = true;
    s.pressStartMs = now;
    s.longFired = false;

    // Did this press come during a DOUBLE window?
    if (s.awaitingDouble && now <= s.awaitExpireMs) {
      // Second press — release will emit DOUBLE. Stop awaiting CLICK.
      // We remember "this is the double path" by keeping awaitingDouble=true
      // but also flagging that we're mid-second-press — handled on release.
    }
    return EVT_NONE;
  }

  // Holding pressed: check LONG
  if (pressed && s.prevPressed) {
    if (!s.longFired && (now - s.pressStartMs) >= LONG_MS) {
      s.longFired = true;
      s.awaitingDouble = false;  // LONG consumes, don't interpret release as click
      return EVT_LONG;
    }
    return EVT_NONE;
  }

  // Edge: release
  if (!pressed && s.prevPressed) {
    s.prevPressed = false;
    uint32_t held = now - s.pressStartMs;

    if (s.longFired) {
      // LONG already handled, ignore release.
      return EVT_NONE;
    }

    if (held > CLICK_MAX_MS) {
      // Treat as canceled press (between CLICK_MAX and LONG thresholds):
      // neither CLICK nor LONG. Drop silently.
      s.awaitingDouble = false;
      return EVT_NONE;
    }

    // It's a quick release. Are we completing a double?
    if (s.awaitingDouble && now <= s.awaitExpireMs) {
      s.awaitingDouble = false;
      s.lastReleaseMs = now;
      return EVT_DOUBLE;
    }

    // First quick release — start DOUBLE window.
    s.awaitingDouble = true;
    s.awaitExpireMs = now + DOUBLE_GAP_MS;
    s.lastReleaseMs = now;
    return EVT_NONE;
  }

  // Idle: check if DOUBLE window expired into a CLICK
  if (!pressed && !s.prevPressed && s.awaitingDouble && now > s.awaitExpireMs) {
    s.awaitingDouble = false;
    return EVT_CLICK;
  }

  return EVT_NONE;
}

InputEvent encoderStep(EncoderFSM& s, float currentDeg) {
  const float DETENT_DEG = 15.0f;
  float delta = currentDeg - s.lastEmitDeg;
  if (delta >= DETENT_DEG) {
    s.lastEmitDeg += DETENT_DEG;
    return EVT_ROT_CW;
  }
  if (delta <= -DETENT_DEG) {
    s.lastEmitDeg -= DETENT_DEG;
    return EVT_ROT_CCW;
  }
  return EVT_NONE;
}

}  // namespace hw_input_internal

#ifdef ARDUINO
// Hardware-specific parts live only in the Arduino build.
// Filled in step 4.8.
static const int BTN_PIN = 5;

static hw_input_internal::ButtonFSM  _btn;
static hw_input_internal::EncoderFSM _enc;

void hw_input_init() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  // MT6701 init deferred to step 4.8
}

InputEvent hw_input_poll() {
  uint32_t now = millis();

  // Button (active low on X-Knob)
  bool pressed = (digitalRead(BTN_PIN) == LOW);
  InputEvent be = hw_input_internal::buttonStep(_btn, pressed, now);
  if (be != EVT_NONE) return be;

  // Encoder — real implementation in step 4.8. For now, placeholder.
  return EVT_NONE;
}
#endif
```

- [ ] **Step 4.6: Run tests — expect PASS**

```bash
pio test -e native -f test_input_fsm
```
Expected: all 5 tests pass.

If failures: read Unity output. A common miss is the "canceled press" case (release between 500 and 600 ms); verify that test pass is consistent with the implementation's behavior for that window.

### 4c. Encoder FSM — TDD

- [ ] **Step 4.7: Write encoder delta tests**

`test/native/test_encoder.cpp`:
```cpp
#include <unity.h>
#include "../../src/hw_input.h"

using namespace hw_input_internal;

void setUp() {}
void tearDown() {}

void test_no_motion_no_event() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 0.0f));
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 5.0f));
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 14.9f));
}

void test_cw_detent() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_ROT_CW, encoderStep(s, 15.0f));
  // Need another 15 deg to emit again
  TEST_ASSERT_EQUAL(EVT_NONE, encoderStep(s, 20.0f));
  TEST_ASSERT_EQUAL(EVT_ROT_CW, encoderStep(s, 30.0f));
}

void test_ccw_detent() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_ROT_CCW, encoderStep(s, -15.0f));
  TEST_ASSERT_EQUAL(EVT_ROT_CCW, encoderStep(s, -30.0f));
}

void test_reversal() {
  EncoderFSM s;
  TEST_ASSERT_EQUAL(EVT_ROT_CW,  encoderStep(s, 15.0f));
  TEST_ASSERT_EQUAL(EVT_NONE,    encoderStep(s, 10.0f));
  // From emit point 15, go to 0 = -15 delta → CCW
  TEST_ASSERT_EQUAL(EVT_ROT_CCW, encoderStep(s, 0.0f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_motion_no_event);
  RUN_TEST(test_cw_detent);
  RUN_TEST(test_ccw_detent);
  RUN_TEST(test_reversal);
  return UNITY_END();
}
```

Run:
```bash
pio test -e native -f test_encoder
```
Expected: all pass (implementation already in place from step 4.5).

### 4d. Wire MT6701 on-device

- [ ] **Step 4.8: Add MT6701 angle reading to `hw_input.cpp`**

MT6701 is an I2C magnetic encoder, 14-bit absolute angle. Replace the `#ifdef ARDUINO` block in `hw_input.cpp` with:
```cpp
#ifdef ARDUINO

static const int BTN_PIN    = 5;
static const int MT6701_SDA = 1;
static const int MT6701_SCL = 2;
static const uint8_t MT6701_ADDR = 0x06;  // 7-bit address

static hw_input_internal::ButtonFSM  _btn;
static hw_input_internal::EncoderFSM _enc;

// MT6701 angle register is 0x03 (high byte) / 0x04 (low 6 bits).
// Value is 14-bit, 0..16383 over 0..360°.
static float mt6701_read_deg() {
  Wire.beginTransmission(MT6701_ADDR);
  Wire.write(0x03);
  if (Wire.endTransmission(false) != 0) return NAN;
  Wire.requestFrom((int)MT6701_ADDR, 2);
  if (Wire.available() < 2) return NAN;
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  uint16_t raw = ((uint16_t)hi << 6) | (lo >> 2);   // 14-bit
  return (raw * 360.0f) / 16384.0f;
}

// Accumulated degrees across wrap (MT6701 returns 0..360; we want unbounded).
static float _accDeg = 0.0f;
static float _lastRawDeg = NAN;

void hw_input_init() {
  pinMode(BTN_PIN, INPUT_PULLUP);
  Wire.begin(MT6701_SDA, MT6701_SCL, 400000);
  // Seed
  float d = mt6701_read_deg();
  if (!isnan(d)) { _lastRawDeg = d; _accDeg = 0.0f; _enc.lastEmitDeg = 0.0f; }
}

InputEvent hw_input_poll() {
  uint32_t now = millis();

  // Encoder: read angle, unwrap, pass to FSM. Emit only one event per poll
  // (caller can call again; the accumulated value sits until consumed).
  float d = mt6701_read_deg();
  if (!isnan(d) && !isnan(_lastRawDeg)) {
    float diff = d - _lastRawDeg;
    if (diff >  180.0f) diff -= 360.0f;   // wrap-around fix
    if (diff < -180.0f) diff += 360.0f;
    _accDeg += diff;
    _lastRawDeg = d;
    InputEvent re = hw_input_internal::encoderStep(_enc, _accDeg);
    if (re != EVT_NONE) return re;
  } else if (!isnan(d)) {
    _lastRawDeg = d;
  }

  // Button (active low on X-Knob)
  bool pressed = (digitalRead(BTN_PIN) == LOW);
  InputEvent be = hw_input_internal::buttonStep(_btn, pressed, now);
  return be;
}
#endif
```

- [ ] **Step 4.9: Replace `src/main.cpp` `loop()` with event printer**

`src/main.cpp` `setup()` must also call `hw_input_init()`; `loop()` becomes:
```cpp
void loop() {
  InputEvent e = hw_input_poll();
  switch (e) {
    case EVT_ROT_CW:  Serial.println("CW");     break;
    case EVT_ROT_CCW: Serial.println("CCW");    break;
    case EVT_CLICK:   Serial.println("CLICK");  break;
    case EVT_DOUBLE:  Serial.println("DOUBLE"); break;
    case EVT_LONG:    Serial.println("LONG");   break;
    default: break;
  }
  delay(5);  // poll ~200 Hz
}
```

Remember to add `#include "hw_input.h"` at top and a call to `hw_input_init();` in `setup()` after `hw_display_init()`.

- [ ] **Step 4.10: Flash and verify event stream**

```bash
pio run -t upload && pio device monitor
```

Expected:
- Turn knob CW slowly → `CW` lines, roughly one per 15° detent.
- Turn knob CCW → `CCW` lines.
- Short press → `CLICK` (after ~300 ms delay for double-check window).
- Two quick presses → `DOUBLE`.
- Press and hold > 600 ms → `LONG`.

If no events on rotation: MT6701 may be at a different I2C address. Run `i2cdetect` equivalent — add to `hw_input_init`:
```cpp
for (int a = 1; a < 127; a++) {
  Wire.beginTransmission(a);
  if (Wire.endTransmission() == 0) Serial.printf("i2c ack at 0x%02x\n", a);
}
```
Reflash, note the address, update `MT6701_ADDR`.

If rotation direction feels inverted relative to approve/deny intuition: negate in `encoderStep` later when we map to actions. Don't change the hardware layer.

- [ ] **Step 4.11: Commit**

```bash
git add .
git commit -m "input: MT6701 encoder + button FSM with native tests"
```

---

## Task 5: `hw_motor` — open-loop bump

**Files:**
- Create: `src/hw_motor.h`, `src/hw_motor.cpp`
- Modify: `src/main.cpp` (call `hw_motor_click` on each input event)

- [ ] **Step 5.1: Write `hw_motor.h`**

`src/hw_motor.h`:
```cpp
#pragma once
#include <stdint.h>

void hw_motor_init();
void hw_motor_click(uint8_t strength);   // strength 0..255, 30 ms pulse
void hw_motor_off();
```

- [ ] **Step 5.2: Write `hw_motor.cpp`**

`src/hw_motor.cpp`:
```cpp
#include "hw_motor.h"
#include <Arduino.h>

static const int MO1 = 17;
static const int MO2 = 16;
static const int MO3 = 15;

// LEDC channels for 3-phase PWM
static const int CH1 = 1;
static const int CH2 = 2;
static const int CH3 = 3;

static const int PWM_FREQ = 20000;   // 20 kHz — above audible
static const int PWM_BITS = 8;

void hw_motor_init() {
  ledcSetup(CH1, PWM_FREQ, PWM_BITS);
  ledcSetup(CH2, PWM_FREQ, PWM_BITS);
  ledcSetup(CH3, PWM_FREQ, PWM_BITS);
  ledcAttachPin(MO1, CH1);
  ledcAttachPin(MO2, CH2);
  ledcAttachPin(MO3, CH3);
  hw_motor_off();
}

void hw_motor_click(uint8_t strength) {
  // Apply a fixed 3-phase vector (120° apart) for ~30 ms, then coast.
  // This produces a mechanical "bump" without needing SimpleFOC closed-loop.
  // The exact vector doesn't matter much — we just want a short impulse.
  uint8_t s  = strength;
  uint8_t s2 = strength / 2;
  ledcWrite(CH1, s);
  ledcWrite(CH2, s2);
  ledcWrite(CH3, 0);
  delay(30);
  hw_motor_off();
}

void hw_motor_off() {
  ledcWrite(CH1, 0);
  ledcWrite(CH2, 0);
  ledcWrite(CH3, 0);
}
```

- [ ] **Step 5.3: Wire into `main.cpp`**

In `setup()` add `hw_motor_init();` after `hw_input_init();`. Modify `loop()` to click on every event:
```cpp
void loop() {
  InputEvent e = hw_input_poll();
  if (e != EVT_NONE) hw_motor_click(120);
  switch (e) {
    case EVT_ROT_CW:  Serial.println("CW");     break;
    case EVT_ROT_CCW: Serial.println("CCW");    break;
    case EVT_CLICK:   Serial.println("CLICK");  break;
    case EVT_DOUBLE:  Serial.println("DOUBLE"); break;
    case EVT_LONG:    Serial.println("LONG");   break;
    default: break;
  }
  delay(5);
}
```

Add `#include "hw_motor.h"` at top.

- [ ] **Step 5.4: Flash and verify**

```bash
pio run -t upload && pio device monitor
```

Expected: turning the knob produces a tactile "bump" at each detent. Pressing the button produces a bump. Bump should be mechanical (you feel it in your fingers) not acoustic (you shouldn't hear a buzz at 20 kHz PWM).

If no bump: wrong phase vector — motor may not produce torque for the chosen vector. Try swapping: `ledcWrite(CH1, 0); ledcWrite(CH2, s); ledcWrite(CH3, s2);` and repeat. Test all 6 permutations until one gives a clear bump.

If the motor keeps spinning or vibrating after the click: `delay(30)` ended but pins didn't go low. Check that `hw_motor_off()` zeros all three channels.

If the bump is too strong / too weak: adjust `strength` arg. 60 is subtle, 200 is noticeable.

- [ ] **Step 5.5: Commit**

```bash
git add .
git commit -m "motor: open-loop bump on every input event"
```

---

## Task 6: `hw_leds` — NeoPixel modes

**Files:**
- Create: `src/hw_leds.h`, `src/hw_leds.cpp`
- Modify: `src/main.cpp` (demo: attention breath on CW, approve flash on CLICK, deny flash on LONG)

- [ ] **Step 6.1: Write `hw_leds.h`**

`src/hw_leds.h`:
```cpp
#pragma once
#include <stdint.h>

enum LedMode {
  LED_OFF = 0,
  LED_ATTENTION_BREATH,
  LED_APPROVE_FLASH,
  LED_DENY_FLASH,
};

void hw_leds_init();
// Set mode. Calls during an active flash (APPROVE/DENY) are ignored until the
// flash completes and auto-reverts to LED_OFF.
void hw_leds_set_mode(LedMode m);
// Advance animation. Call once per main-loop tick.
void hw_leds_tick();
```

- [ ] **Step 6.2: Write `hw_leds.cpp`**

`src/hw_leds.cpp`:
```cpp
#include "hw_leds.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <math.h>

static const int      LED_PIN   = 38;
static const uint16_t LED_COUNT = 8;

static Adafruit_NeoPixel _strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
static LedMode  _mode = LED_OFF;
static uint32_t _modeStart = 0;

static void fill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < LED_COUNT; i++) _strip.setPixelColor(i, r, g, b);
  _strip.show();
}

void hw_leds_init() {
  _strip.begin();
  _strip.setBrightness(80);
  fill(0, 0, 0);
  _mode = LED_OFF;
}

void hw_leds_set_mode(LedMode m) {
  // Don't interrupt an active flash.
  if (_mode == LED_APPROVE_FLASH || _mode == LED_DENY_FLASH) return;
  if (m == _mode) return;
  _mode = m;
  _modeStart = millis();
}

void hw_leds_tick() {
  uint32_t t = millis() - _modeStart;
  switch (_mode) {
    case LED_OFF:
      fill(0, 0, 0);
      break;

    case LED_ATTENTION_BREATH: {
      // 1.2 s period, red.
      float phase = (t % 1200) / 1200.0f;              // 0..1
      float v = 0.5f - 0.5f * cosf(phase * 2.0f * PI); // 0..1
      uint8_t r = (uint8_t)(v * 200.0f);
      fill(r, 0, 0);
      break;
    }

    case LED_APPROVE_FLASH: {
      // 3 flashes over 900 ms: 150 on, 150 off × 3 = 900
      uint32_t cycle = t % 300;
      bool on = cycle < 150;
      fill(on ? 0 : 0, on ? 200 : 0, on ? 0 : 0);
      if (t >= 900) { _mode = LED_OFF; fill(0,0,0); }
      break;
    }

    case LED_DENY_FLASH: {
      uint32_t cycle = t % 300;
      bool on = cycle < 150;
      fill(on ? 200 : 0, 0, 0);
      if (t >= 900) { _mode = LED_OFF; fill(0,0,0); }
      break;
    }
  }
}
```

- [ ] **Step 6.3: Wire into main.cpp demo**

Modify `setup()` to call `hw_leds_init()`. Modify `loop()`:
```cpp
void loop() {
  InputEvent e = hw_input_poll();
  switch (e) {
    case EVT_ROT_CW:
      Serial.println("CW");
      hw_leds_set_mode(LED_ATTENTION_BREATH);
      hw_motor_click(120);
      break;
    case EVT_ROT_CCW:
      Serial.println("CCW");
      hw_leds_set_mode(LED_OFF);
      hw_motor_click(120);
      break;
    case EVT_CLICK:
      Serial.println("CLICK");
      hw_leds_set_mode(LED_APPROVE_FLASH);
      hw_motor_click(180);
      break;
    case EVT_LONG:
      Serial.println("LONG");
      hw_leds_set_mode(LED_DENY_FLASH);
      hw_motor_click(200);
      break;
    case EVT_DOUBLE:
      Serial.println("DOUBLE");
      hw_motor_click(150);
      break;
    default: break;
  }
  hw_leds_tick();
  delay(5);
}
```

Add `#include "hw_leds.h"`.

- [ ] **Step 6.4: Flash and verify**

```bash
pio run -t upload && pio device monitor
```

Expected:
- CW → LEDs start red breathing.
- CCW → LEDs go off.
- CLICK → 3 green flashes, then off (cannot be interrupted).
- LONG → 3 red flashes, then off.
- DOUBLE → motor bump, no LED change.

If LEDs don't light at all: check pin 38 wiring in `Adafruit_NeoPixel` constructor and the strip lib version pinned in `platformio.ini`.
If colors are swapped (red showing as green): change `NEO_GRB` to `NEO_RGB` or `NEO_BRG`.

- [ ] **Step 6.5: Commit**

```bash
git add .
git commit -m "leds: NeoPixel modes with self-terminating flash"
```

---

## Task 7: Port buddy renderer (capybara only)

Now we have all hardware in place. Time to get ONE character rendering on the screen.

**Files:**
- Create (copy verbatim from upstream): `src/buddy.h`, `src/buddy_common.h`, `src/buddies/capybara.cpp`
- Create with modifications: `src/buddy.cpp` (sprite ref + coord constants)

- [ ] **Step 7.1: Copy interface headers**

```bash
cd ~/code/opensources/claude-desktop-buddy-xknob/src
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/buddy.h .
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/buddy_common.h .
mkdir -p buddies
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/buddies/capybara.cpp buddies/
```

- [ ] **Step 7.2: Copy `buddy.cpp` from upstream**

```bash
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/buddy.cpp .
```

- [ ] **Step 7.3: Modify `buddy.cpp` — swap sprite extern and coord constants**

Open `src/buddy.cpp`. Make these edits:

**(a)** Replace the M5 include and sprite extern at the top:
```cpp
// WAS:
#include "buddy.h"
#include "buddy_common.h"
#include <M5StickCPlus.h>
#include <string.h>

extern TFT_eSprite spr;
```
with:
```cpp
#include "buddy.h"
#include "buddy_common.h"
#include <string.h>
#include "hw_display.h"

#define spr hw_display_sprite()
```

**(b)** Change coordinate constants for 240×240:
```cpp
// WAS:
const int BUDDY_X_CENTER = 67;
const int BUDDY_CANVAS_W = 135;
const int BUDDY_Y_BASE   = 30;
const int BUDDY_Y_OVERLAY = 6;
```
to:
```cpp
const int BUDDY_X_CENTER = 120;
const int BUDDY_CANVAS_W = 240;
const int BUDDY_Y_BASE   = 70;
const int BUDDY_Y_OVERLAY = 46;
```

**(c)** Reduce the species registry to just capybara for Phase 1. Replace the whole registry block:
```cpp
// WAS:
extern const Species CAPYBARA_SPECIES;
extern const Species DUCK_SPECIES;
... (18 lines of extern)

static const Species* SPECIES_TABLE[] = {
  &CAPYBARA_SPECIES, ...
};
```
with:
```cpp
extern const Species CAPYBARA_SPECIES;

static const Species* SPECIES_TABLE[] = {
  &CAPYBARA_SPECIES,
};
```

**(d)** Stub out `#include "stats.h"` and `speciesIdxLoad/Save` calls if they're referenced. Inspect buddy.cpp for these references and replace calls to:
- `speciesIdxLoad()` → return `0`
- `speciesIdxSave(idx)` → no-op

Simplest fix: add at the top of `buddy.cpp`:
```cpp
// Phase 1 stubs — real NVS persistence arrives in Task 9.
static uint8_t speciesIdxLoad() { return 0; }
static void    speciesIdxSave(uint8_t) {}
```
...and remove `#include "stats.h"` if present. Then the file compiles standalone.

- [ ] **Step 7.4: Wire buddy into main.cpp**

Add `#include "buddy.h"`. In `setup()`, after `hw_display_init()`, add:
```cpp
buddyInit();
```

Replace the `loop()` with a renderer:
```cpp
void loop() {
  static uint8_t state = 1;  // 1 = idle
  InputEvent e = hw_input_poll();
  if (e == EVT_ROT_CW)  state = (state + 1) % 7;
  if (e == EVT_ROT_CCW) state = (state + 6) % 7;
  if (e != EVT_NONE) hw_motor_click(120);

  TFT_eSprite& spr = hw_display_sprite();
  spr.fillSprite(TFT_BLACK);
  buddyTick(state);

  // Label the current state for debugging
  const char* names[] = {"sleep","idle","busy","attention","celebrate","dizzy","heart"};
  spr.setTextColor(TFT_WHITE);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(1);
  spr.drawString(names[state], 120, 200);

  spr.pushSprite(0, 0);
  hw_leds_tick();
  delay(20);
}
```

- [ ] **Step 7.5: Flash and verify character renders**

```bash
pio run -t upload && pio device monitor
```

Expected: a capybara ASCII art appears on screen, centered-ish. Label at bottom shows current state name. Turning the knob cycles through 7 states, and the character's pose / animation changes.

If the character is off-center: adjust `BUDDY_Y_BASE` in `buddy.cpp` up or down by 10 px and re-test. The upstream art was designed for a 135×240 rectangle, so it may need vertical re-center on 240×240.

If text is garbled: font not loaded. Confirm `LOAD_FONT2` is in build_flags.

If one state (e.g., heart) shows no animation: upstream capybara.cpp may have overlays drawn above `BUDDY_Y_BASE`. If the overlay is clipped, adjust `BUDDY_Y_OVERLAY`.

- [ ] **Step 7.6: Commit**

```bash
git add .
git commit -m "buddy: port capybara species with 240x240 coord remap"
```

---

## Task 8: Wire end-to-end — dataPoll + approval flow

Now the final integration: BLE data drives the character state, prompts trigger approval UI, knob+click approves/denies.

**Files:**
- Copy from upstream: `src/character.h`, `src/character.cpp`
- Inline the upstream `data` and minimal `stats` logic into `main.cpp` (or port as separate .cpp files if upstream ships them). See step 8.1.
- Rewrite: `src/main.cpp` (final Phase 1 version)

- [ ] **Step 8.1: Decide how to port `data` and `stats`**

Run:
```bash
ls /Users/zinklu/code/opensources/claude-desktop-buddy/src/data.cpp \
   /Users/zinklu/code/opensources/claude-desktop-buddy/src/stats.cpp 2>/dev/null
```
- If both `.cpp` files exist upstream: copy them verbatim.
```bash
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/data.cpp src/
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/stats.cpp src/
```
- If they don't exist, the logic is inlined in upstream `main.cpp`. Extract the functions (`dataPoll`, `dataBtActive`, `dataRtcValid`, `dataDemo`, `dataSetDemo`, `dataScenarioName`, `statsLoad`, `statsSave`, `statsOnApproval`, `statsOnDenial`, `statsPollLevelUp`, `statsOnWake`, `statsOnNapEnd`, `speciesIdxLoad/Save`, `ownerName`, `petName`, `petNameLoad`, `settingsLoad`, `settingsSave`, `settings()`) into new `src/data.cpp` and `src/stats.cpp`. Mirror the upstream implementations 1:1.

For Phase 1, if extraction is too large, stub the functions we don't use:
- Phase 1 needs: `dataPoll`, `statsOnApproval(uint32_t)`, `statsOnDenial()`.
- Phase 1 can stub to no-ops: `statsOnNapEnd`, `statsOnWake`, `statsPollLevelUp` (return false), `settingsLoad/Save`, `ownerName/petName/petNameLoad`, `speciesIdxLoad/Save`.

If in doubt, ask the user before continuing — the upstream layout drives the choice here.

- [ ] **Step 8.2: Copy character renderer (GIF)**

```bash
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/character.h src/
cp /Users/zinklu/code/opensources/claude-desktop-buddy/src/character.cpp src/
```

Check for M5 deps:
```bash
grep -n 'M5\|Axp' src/character.cpp
```
If matches: inspect. Typically `M5.Lcd` is the only dependency (as a `TFT_eSPI*` render target). Replace with `&hw_display_tft()` or `&hw_display_sprite()` as appropriate.

- [ ] **Step 8.3: Write the final Phase 1 `main.cpp`**

`src/main.cpp`:
```cpp
#include <Arduino.h>
#include <LittleFS.h>
#include "esp_mac.h"
#include "ble_bridge.h"
#include "data.h"
#include "stats.h"
#include "buddy.h"
#include "character.h"
#include "hw_display.h"
#include "hw_input.h"
#include "hw_motor.h"
#include "hw_leds.h"
#include "hw_power.h"

enum PersonaState { P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART };

static char btName[16] = "Claude";
static TamaState tama;
static PersonaState activeState = P_IDLE;
static bool approvalChoice = true;   // true = approve highlighted
static char lastPromptId[40] = "";
static bool responseSent = false;
static uint32_t promptArrivedMs = 0;
static bool gifAvailable = false;
static bool buddyMode = true;

static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

static void sendCmd(const char* json) {
  Serial.println(json);
  bleWrite((const uint8_t*)json, strlen(json));
  bleWrite((const uint8_t*)"\n", 1);
}

static PersonaState derive(const TamaState& s) {
  if (!s.connected)           return P_IDLE;
  if (s.sessionsWaiting > 0)  return P_ATTENTION;
  if (s.recentlyCompleted)    return P_CELEBRATE;
  if (s.sessionsRunning >= 3) return P_BUSY;
  return P_IDLE;
}

static void drawApproval() {
  TFT_eSprite& spr = hw_display_sprite();
  uint16_t bg = TFT_BLACK;

  // Bottom panel, y=160..215
  spr.fillRect(0, 160, 240, 55, bg);
  spr.drawFastHLine(24, 160, 192, TFT_DARKGREY);

  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  spr.setTextColor(waited >= 10 ? TFT_ORANGE : TFT_DARKGREY, bg);
  char line[32];
  snprintf(line, sizeof(line), "approve? %lus", (unsigned long)waited);
  spr.setCursor(28, 166);
  spr.print(line);

  // Tool name, larger
  spr.setTextColor(TFT_WHITE, bg);
  spr.setTextSize(2);
  spr.setCursor(28, 178);
  spr.print(tama.promptTool[0] ? tama.promptTool : "?");
  spr.setTextSize(1);

  // Two-choice row at y=205
  spr.setCursor(36, 205);
  if (approvalChoice) { spr.setTextColor(TFT_GREEN, bg); spr.print("> APPROVE"); }
  else                { spr.setTextColor(TFT_DARKGREY, bg); spr.print("  approve"); }
  spr.setCursor(140, 205);
  if (!approvalChoice){ spr.setTextColor(TFT_RED, bg);   spr.print("> DENY"); }
  else                { spr.setTextColor(TFT_DARKGREY, bg); spr.print("  deny"); }
}

static void drawHudSimple() {
  TFT_eSprite& spr = hw_display_sprite();
  spr.fillRect(0, 170, 240, 30, TFT_BLACK);
  spr.setTextDatum(MC_DATUM);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  const char* line = tama.nLines ? tama.lines[tama.nLines - 1] : tama.msg;
  if (line && *line) spr.drawString(line, 120, 185);
  spr.setTextDatum(TL_DATUM);
}

void setup() {
  hw_power_init();
  Serial.begin(115200);
  delay(200);
  Serial.println("xknob-buddy: boot");

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

  hw_display_init();
  hw_input_init();
  hw_motor_init();
  hw_leds_init();

  statsLoad();
  buddyInit();
  characterInit(nullptr);
  gifAvailable = characterLoaded();
  buddyMode = !gifAvailable;   // Default: use GIF if installed, else ASCII

  startBt();
  Serial.printf("advertising as %s\n", btName);
}

void loop() {
  uint32_t now = millis();

  dataPoll(&tama);
  activeState = derive(tama);

  bool inPrompt = tama.promptId[0] && !responseSent;

  // Detect new prompt
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    approvalChoice = true;
    if (tama.promptId[0]) promptArrivedMs = now;
  }

  // Input routing
  InputEvent e = hw_input_poll();
  if (inPrompt) {
    switch (e) {
      case EVT_ROT_CW:
      case EVT_ROT_CCW:
        approvalChoice = !approvalChoice;
        hw_motor_click(80);
        break;
      case EVT_CLICK: {
        char cmd[96];
        snprintf(cmd, sizeof(cmd), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}",
                 tama.promptId, approvalChoice ? "once" : "deny");
        sendCmd(cmd);
        responseSent = true;
        if (approvalChoice) {
          statsOnApproval((now - promptArrivedMs) / 1000);
          hw_leds_set_mode(LED_APPROVE_FLASH);
        } else {
          statsOnDenial();
          hw_leds_set_mode(LED_DENY_FLASH);
        }
        hw_motor_click(200);
        break;
      }
      default: break;
    }
  }
  // Phase 1: no home-screen input handling. All non-prompt events ignored.

  // LED
  if (!inPrompt) {
    hw_leds_set_mode(activeState == P_ATTENTION ? LED_ATTENTION_BREATH : LED_OFF);
  }
  hw_leds_tick();

  // Render
  TFT_eSprite& spr = hw_display_sprite();
  if (buddyMode) {
    buddyTick((uint8_t)activeState);
  } else if (characterLoaded()) {
    characterSetState((uint8_t)activeState);
    characterTick();
  } else {
    spr.fillSprite(TFT_BLACK);
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.drawString("no character", 120, 120);
    spr.setTextDatum(TL_DATUM);
  }

  if (inPrompt) drawApproval();
  else          drawHudSimple();

  spr.pushSprite(0, 0);

  delay(16);
}
```

- [ ] **Step 8.4: Build — triage link errors**

```bash
pio run
```

Expected issues and fixes:
- `undefined reference to 'dataPoll'` etc. → step 8.1 didn't produce a valid implementation. Add stubs in a new `src/data.cpp` for the missing symbols and defer full impl to Phase 2. For Phase 1 minimum:
  ```cpp
  // src/data.cpp (stub — replace with upstream impl)
  #include "data.h"
  void dataPoll(TamaState* s) { s->connected = bleConnected(); }
  bool dataBtActive() { return bleConnected(); }
  bool dataRtcValid() { return false; }
  bool dataDemo() { return false; }
  void dataSetDemo(bool) {}
  const char* dataScenarioName() { return "ble"; }
  ```
- `undefined reference to 'statsLoad'/'statsOnApproval'/etc.` → same pattern, create `src/stats.cpp` stubs:
  ```cpp
  #include "stats.h"
  void statsLoad() {}
  void statsSave() {}
  void statsOnApproval(uint32_t) {}
  void statsOnDenial() {}
  void statsOnWake() {}
  void statsOnNapEnd(uint32_t) {}
  bool statsPollLevelUp() { return false; }
  uint8_t statsMoodTier()  { return 2; }
  uint8_t statsFedProgress() { return 0; }
  uint8_t statsEnergyTier() { return 3; }
  const Stats& stats() { static Stats s{}; return s; }
  ```
  Cross-check exact signatures against upstream `stats.h`. If the Stats struct has required fields, initialize them.
- If `character.cpp` references M5: see step 8.2, replace.

Iterate until build succeeds.

- [ ] **Step 8.5: Flash and verify full loop**

```bash
pio run -t upload && pio device monitor
```

Expected sequence:
1. Boot → "boot" / "advertising as Claude-XXXX"
2. Screen shows capybara (or no-character if no GIF / buddy stub)
3. Pair with Claude Desktop (same passkey flow as Task 1)
4. Once connected, `tama.connected = true`, activeState transitions based on sessions data
5. Trigger a prompt in Claude Desktop (e.g., run a Claude session that asks for permission)
6. Approval panel appears at bottom of screen, approve is highlighted green
7. Turn knob → deny highlights red, motor clicks
8. Click button → `LED_APPROVE_FLASH` or `LED_DENY_FLASH` plays, panel disappears, desktop receives decision
9. Character re-appears as main view

If prompts don't appear: check `dataPoll` actually parses incoming JSON. This is the most likely place Phase 1 stubs fall short.
If decisions don't reach Claude Desktop: check `bleWrite` in `sendCmd`. Put a serial print to confirm the JSON is being sent.

- [ ] **Step 8.6: Commit**

```bash
git add .
git commit -m "main: Phase 1 MVP — BLE + character + approval end-to-end"
```

---

## Task 9: Phase 1 acceptance checklist

This is the gate before calling Phase 1 "done." Run each check; if any fail, file a follow-up and re-open the relevant task.

- [ ] **Step 9.1: Boot time < 5 seconds**

Power-cycle. From USB attach to `advertising as Claude-XXXX` on serial. Record duration. Acceptable: ≤ 5 s. If slower, investigate `LittleFS.begin()` (format on first boot can be slow — subsequent boots should be fast).

- [ ] **Step 9.2: Pairing works with a fresh Claude Desktop install**

Unpair previous sessions (bleClearBonds not exposed in Phase 1, so just unpair from Claude Desktop side). Pair again, enter passkey, confirm `bleSecure()` becomes true (add a serial print if needed).

- [ ] **Step 9.3: Character state changes respond to real Claude activity**

Start a Claude session → P_IDLE. Have 3+ sessions running → P_BUSY. Let a session request permission → P_ATTENTION + prompt panel.

- [ ] **Step 9.4: Approve and deny both round-trip**

Generate a prompt. Approve it. Confirm Claude Desktop acts on the approval. Generate another. Turn knob to deny, click. Confirm denial.

- [ ] **Step 9.5: FPS ≥ 25 with character rendering**

Add temporary fps counter to `loop()`. With capybara rendering, should be ≥ 25 fps.

- [ ] **Step 9.6: NeoPixel doesn't glitch during flash playback**

Trigger rapid approvals (5 in a row). LEDs should play each flash fully without interrupting. If they stutter, audit `hw_leds_set_mode`'s flash-in-progress guard.

- [ ] **Step 9.7: Motor doesn't get hot**

After 5 minutes of fiddling, back of the device should be cool or slightly warm but not hot. If hot: `hw_motor_off` isn't zeroing PWM, OR the bump duration is accidentally longer. Check current consumption at idle.

- [ ] **Step 9.8: Tag Phase 1 release**

```bash
git tag -a phase-1 -m "Phase 1 MVP: BLE + capybara + approval"
```

---

## Phase 1 complete

Phase 2 items (menus, Info pages, Pet pages, all 18 species, GIF char packs, nap/dizzy/shake, clock mode, passkey display UI, reset menu, power-off menu item, fast-rotate dizzy trigger, deep sleep) are tracked in the spec's §7 but out of scope for this plan.
