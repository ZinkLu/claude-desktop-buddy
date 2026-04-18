# X-Knob 移植设计（claude-desktop-buddy → ESP32-S3 + 圆屏 + 旋钮）

目标硬件：[X-Knob](https://github.com/Makerfabs/Project_X-Knob) 开源项目（ESP32-S3 DevKitC-1，240×240 GC9A01 圆屏，BLDC + MT6701 旋钮，Adafruit NeoPixel ×8，单按钮）。

源工程：[claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) M5StickC Plus 版固件（ESP32 + AXP192 + 135×240 ST7789V + MPU6886 + 双按钮）。

交付策略：**Phase 1 先让 buddy 活过来（MVP），Phase 2 再补齐功能对等**。本文档只完整定义 Phase 1；Phase 2 细节待 Phase 1 实装后再写补充设计。

仓库策略：**fork 成独立仓库**（建议名 `claude-desktop-buddy-xknob`），不做 HAL 抽象不维护双设备。

---

## 1. 目标与非目标

### 目标（Phase 1 与 Phase 2 共同遵守）

- **Claude Desktop 端零改动**。BLE 服务/特征 UUID、帧格式、passkey 配对流程、字符包流式传输协议 (`xfer.h`) 完全保持和 M5StickC Plus 版一致。
- **兼容性锚点**（代码中不可擅动）：
  - `ble_bridge.{h,cpp}` 的 UUID、帧协议、passkey、bond 存储
  - `xfer.h` 字符包传输协议
  - `data.h` / `stats.h` 的 NVS 键名（namespace `buddy` 内 `species`、`owner`、`petname`、`settings`、`stats`）
  - `buddies/*.cpp` 18 个角色的 `const Species NAME_SPECIES = {...}` 接口
- **最终（Phase 2 结束时）保留所有原版功能**：18 种 ASCII 角色、GIF 字符包、审批流、菜单 / 设置 / 重置面板、Info 6 页、Pet 2 页、统计、NVS 持久化、时钟模式、所有一次性动效（celebrate / dizzy / nap / attention）。

### 非目标

- 不做 HAL 抽象层，不维护双设备编译。
- 不做上游同步自动化；上游新角色用文件 cherry-pick（`buddies/*.cpp` 对硬件无依赖）。
- 不新增 Claude Desktop 端字段；不新增 BLE 命令。
- **不复刻横屏时钟**（圆屏无横竖概念，直接删）。
- 不复刻硬件不具备的 Info 行：电池电压/电流/AXP 温度 → 隐藏或显示 "n/a"。
- 不做 WiFi/MQTT/网页配置（X-Knob 原版有，buddy 语义是 BLE-only）。

---

## 2. 硬件映射总表

| 原功能 | M5StickC Plus | X-Knob 实现 |
|---|---|---|
| 屏幕 | 135×240 ST7789V, TFT_eSprite | **240×240 GC9A01, TFT_eSprite**（115 KB，尽量分配在 PSRAM） |
| 输入主体 | BtnA / BtnB / Power | **MT6701 旋钮 + 单按钮 (pin 5)**，上下文敏感状态机 |
| 触觉 / 音效 | 被动蜂鸣器 | **BLDC 电机 haptic click**（每 detent 一次脉冲）+ NeoPixel 闪光 |
| 提示灯 | 单红 LED pin 10, active-low | **8 颗 NeoPixel pin 38**（attention 呼吸红 / approve 绿闪 / deny 红闪） |
| 亮度 | `M5.Axp.ScreenBreath(20..100)` | **PWM on TFT_BLK = pin 13**, 5 档 |
| 屏幕关闭 | `M5.Axp.SetLDO2(false)`（LDO 断电） | **背光 PWM = 0**（面板仍上电） |
| 关机菜单项 | `M5.Axp.PowerOff()` | **拉低 `ON_OFF_PIN = 18`**（X-Knob 原生物理关断路径，**非 deep sleep**） |
| 时间源 | 外挂 RTC BM8563 | **ESP32-S3 内置 RTC**，BLE 同步。无电池备份 → 断电丢失，属已知回归 |
| 加速度 / 姿态 | MPU6886 | **无**。全部用旋钮行为替代：`ROT_FAST` → shake→dizzy；`BTN_LONG_3000` → nap 进入 / 退出 |
| 电量 / 温度 | AXP 寄存器 | Info 页对应行显 "n/a" 或隐藏 |
| 电源按钮 | `M5.Axp.GetBtnPress()` | **旋钮 `BTN_DOUBLE`** 代替（屏幕开/关） |

引脚取自 X-Knob `src/config.h`（上游仓库）：
- SPI 显示：SCLK=12, MOSI=11, CS=10, DC=14, RST=9, BLK=13
- BLDC：MO1=17, MO2=16, MO3=15
- MT6701：SDA=1, SCL=2, SS=42
- 按钮：pin 5；ON_OFF=18；NeoPixel：pin 38（8 颗 GRB）

---

## 3. 仓库与文件组织

新仓库根：

```
claude-desktop-buddy-xknob/
├── platformio.ini                    # env:x-knob, board=esp32-s3-devkitc-1
├── partitions.csv                    # 仿 X-Knob（16 MB flash + LittleFS）
├── src/
│   ├── main.cpp                      # 主循环、输入派发、setup
│   ├── ble_bridge.{h,cpp}            # 原样保留
│   ├── xfer.h                        # 原样保留
│   ├── data.h                        # 原样保留
│   ├── stats.{h,cpp}                 # 原样保留
│   ├── character.{h,cpp}             # GIF 渲染，只替换目标 sprite 变量
│   ├── buddy.{h,cpp}, buddy_common.h # 接口层不动，坐标常量改为 240×240
│   ├── buddies/*.cpp                 # 18 个角色原样保留
│   │
│   ├── hw_display.{h,cpp}            # GC9A01 init, 背光 PWM, sprite 全局
│   ├── hw_input.{h,cpp}              # MT6701 + 按钮事件
│   ├── hw_motor.{h,cpp}              # BLDC open-loop bump（Phase 1 不跑 SimpleFOC 闭环）
│   ├── hw_leds.{h,cpp}               # NeoPixel 封装：attention/approve/deny/off
│   ├── hw_power.{h,cpp}              # ON_OFF 物理关机、系统时间
│   │
│   └── input_fsm.{h,cpp}             # Phase 2：上下文敏感状态机。Phase 1 直接在 main 里 switch
├── tools/                            # 原样保留（Python 字符包工具）
├── characters/                       # 原样保留
└── docs/                             # 原样保留 + 新增 X-Knob 硬件笔记
```

**重写原则**：不用 `#ifdef` 做双平台，**直接替换**所有 `M5.*` 调用：

| 原调用 | 替换 |
|---|---|
| `M5.Lcd.*` | `tft.*`（`TFT_eSPI tft` 配置为 GC9A01） |
| `M5.BtnA.wasReleased()` / `M5.BtnB.wasPressed()` | `hw_input_poll()` 返回的事件 switch |
| `M5.Axp.ScreenBreath(n)` | `hw_display_set_brightness(pct)` |
| `M5.Axp.SetLDO2(false)` | `hw_display_sleep()` |
| `M5.Imu.getAccelData(...)` | 删除（旋钮事件替代） |
| `M5.Beep.tone(freq, dur)` | `hw_motor_click(strength)` + 可选 `hw_leds_flash(color, ms)` |
| `M5.Rtc.GetTime/Date` | `hw_power_get_time()`（`time()` + `localtime`） |
| `M5.Axp.PowerOff()` | `hw_power_off()` |
| `M5.Axp.GetBtnPress()` | 事件 `EVT_DOUBLE` |
| `M5.Axp.GetBatVoltage/GetBatCurrent/GetVBusVoltage/GetTempInAXP192` | 返回固定 "n/a" sentinel（Phase 2 Info 页决定隐藏还是显示占位） |

`buddies/*.cpp` 18 个文件保持原样的关键：它们用 `buddy_common.h` 里的 `buddyPrintSprite/buddySetCursor/buddyPrint/buddySetColor` 封装，最终写入 `TFT_eSPI*` 指针。只要在 `buddy.cpp` 里 `extern TFT_eSprite spr` 指向新的 240×240 sprite 即可；坐标常量由 `BUDDY_X_CENTER=67 / BUDDY_Y_BASE=30` 改为 `120 / 70`。

`character.cpp`（GIF 渲染）同理：`characterRenderTo(TFT_eSPI*, x, y)` 签名已经抽好，只改调用点坐标，解码/调色板不动。

---

## 4. Phase 1：让 buddy 活过来（MVP）

### 4.1 MVP 定义

上电 → 屏幕亮 → BLE 广播 `Claude-XXXX` → Claude Desktop 连上 → 圆屏居中画出一个 ASCII 角色，随 Claude 状态（idle/busy/attention/celebrate）切表情 → prompt 到来能 approve / deny。

### 4.2 包含

- `hw_display`：GC9A01 初始化、240×240 sprite、PWM 亮度固定一档（50%）
- `hw_input`：旋钮 `ROT_CW/ROT_CCW` + 按钮 `CLICK/DOUBLE/LONG`（不做 `FAST` 和 `LONG_3000`）
- `hw_motor`：最简单的 detent click（open-loop 脉冲一个函数）
- `hw_leds`：三个函数 `attention_breath / approve_flash / deny_flash` + `off`
- `ble_bridge` / `data` / `stats` / `character` / `buddies/` **原样搬**
- `main.cpp` 砍掉所有菜单/设置/Info/Pet/时钟/nap/dizzy/shake 逻辑；保留：主循环、`derive()` 状态派生、简化 HUD（只显最新一行）、审批流
- BLE 协议、字符包传输、NVS 持久化全部保留

### 4.3 不做（推到 Phase 2）

菜单、设置、Info 6 页、Pet 2 页、ascii pet 切换 UI、nap、dizzy、shake、celebrate 特效、时钟模式、passkey 显示 UI（BLE 栈里的 passkey 生成保留，配对时走串口打印）、reset 菜单、deep sleep、输入上下文状态机（Phase 1 直接在 main 里 switch 足够）。

### 4.4 屏幕布局（240×240 圆屏）

**坐标常量迁移**（原版 135×240 → 新版 240×240）：

| 原版 | 新值 |
|---|---|
| `W = 135, H = 240` | `W = 240, H = 240` |
| `CX = 67` | `CX = 120` |
| `BUDDY_X_CENTER = 67` | `120` |
| `BUDDY_Y_BASE = 30` | `70` |
| `BUDDY_Y_OVERLAY = 6` | `46` |
| HUD `WIDTH = 21` 字符 | `32` 字符（240 / 6 ≈ 40，留左右各 24 px 边距） |
| HUD `AREA = 78` | `70` |
| 左右文字边距 `x = 4` | `x = 24` |

**圆形裁切约束**：
- `y < 24` 和 `y > 215`：不放任何内容
- `y ∈ [24, 60] ∪ [180, 215]`：只放短文字居中
- `y ∈ [60, 180]`：主力区，全宽可用

**Phase 1 分区**：
- `y=0..24`：留空
- `y=24..150`：角色主渲染区（角色本体 + 粒子）
- `y=150..170`：可选状态标签
- `y=170..215`：HUD / 审批面板
- `y=215..240`：留空

**审批面板**（Phase 1 版）：
```
        approve? 3s         ← y=170，小字 + 计数秒
        Read                ← y=185，大字工具名
      > APPROVE    deny     ← y=205，高亮项绿色
        approve   > DENY    ← 旋钮转动切换高亮
```
旋钮转动切高亮，点击执行。电机在两项间切换时 click。

**常规 HUD**（Phase 1 版）：只显最新一行 `tama.msg` 或 `tama.lines[last]`，居中、不换行（超出裁掉），y=170 附近。历史翻页留 Phase 2。

### 4.5 外设 API（Phase 1 完整签名）

```cpp
// hw_display.h
void         hw_display_init();
void         hw_display_set_brightness(uint8_t pct);   // 0..100
extern TFT_eSprite spr;                                // 240×240

// hw_input.h
enum InputEvent { EVT_NONE, EVT_ROT_CW, EVT_ROT_CCW,
                  EVT_CLICK, EVT_DOUBLE, EVT_LONG };
void         hw_input_init();
InputEvent   hw_input_poll();                          // 主循环每 tick 调用

// hw_motor.h
void         hw_motor_init();
void         hw_motor_click(uint8_t strength);         // ~30 ms 脉冲
void         hw_motor_off();

// hw_leds.h
enum LedMode { LED_OFF, LED_ATTENTION_BREATH,
               LED_APPROVE_FLASH, LED_DENY_FLASH };
void         hw_leds_init();
void         hw_leds_set_mode(LedMode m);
void         hw_leds_tick();                           // 每 loop 推进动画

// hw_power.h
void         hw_power_init();                          // ON_OFF_PIN 维持高电平
void         hw_power_off();                           // Phase 1 不调用，接口占位
time_t       hw_power_now();                           // 封装 time(nullptr)
```

**TFT_eSPI 配置**（在 `platformio.ini` `build_flags` 传，避免污染全局 `User_Setup.h`）：
```
-DUSER_SETUP_LOADED
-DGC9A01_DRIVER
-DTFT_MISO=-1 -DTFT_MOSI=11 -DTFT_SCLK=12
-DTFT_CS=10 -DTFT_DC=14 -DTFT_RST=9
-DTFT_WIDTH=240 -DTFT_HEIGHT=240
-DSPI_FREQUENCY=40000000
```

**MT6701 事件生成**：每读到的角度跨过 `360°/24 = 15°` 阈值触发一次 `ROT_CW` 或 `ROT_CCW`。X-Knob 的 `hal/encoder.cpp` 是参考。

**按钮事件**：pin 5 外部中断 + 软件去抖。阈值：`CLICK` < 500 ms 释放；`DOUBLE` 两次 click 间 < 300 ms；`LONG` 按住 600 ms 触发（参考原版 BtnA 长按）。

**电机 bump**：三相 PWM 固定向量 20 ms 脉冲再释放。**不跑 SimpleFOC 闭环**。

**NeoPixel 动画模式**：
- `LED_OFF`：全灭
- `LED_ATTENTION_BREATH`：红色呼吸，周期 1.2 s
- `LED_APPROVE_FLASH`：绿色闪 3 下后自动回 `LED_OFF`
- `LED_DENY_FLASH`：红色闪 3 下后自动回 `LED_OFF`

**`hw_leds_set_mode` 语义**：当前模式为 `LED_APPROVE_FLASH` / `LED_DENY_FLASH` 且尚未播完时，`set_mode` 调用会**被忽略**（flash 优先）。`hw_leds_tick` 内部判定 flash 播完后自动切回 `LED_OFF`，之后 `set_mode` 恢复可写。这样主循环每 tick 都可以无脑调用 `hw_leds_set_mode(attention ? BREATH : OFF)` 而不会打断 flash。

### 4.6 main.cpp 骨架（Phase 1）

```cpp
void setup() {
  Serial.begin(115200);
  hw_power_init();
  hw_display_init();
  hw_input_init();
  hw_motor_init();
  hw_leds_init();

  LittleFS.begin();
  statsLoad();
  settingsLoad();
  petNameLoad();
  buddyInit();
  characterInit(nullptr);

  startBt();  // BLE init, advertise Claude-XXXX
}

void loop() {
  uint32_t now = millis();

  dataPoll(&tama);
  baseState = derive(tama);
  if ((int32_t)(now - oneShotUntil) >= 0) activeState = baseState;

  // 输入事件派发
  InputEvent evt = hw_input_poll();
  bool inPrompt = tama.promptId[0] && !responseSent;

  // approvalChoice: true = 高亮 approve, false = 高亮 deny
  static bool approvalChoice = true;

  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId)-1);
    responseSent = false;
    approvalChoice = true;  // 每次新 prompt 默认高亮 approve
    if (tama.promptId[0]) promptArrivedMs = now;
  }

  if (inPrompt) {
    switch (evt) {
      case EVT_ROT_CW:
      case EVT_ROT_CCW:
        approvalChoice = !approvalChoice;
        hw_motor_click(60);
        break;
      case EVT_CLICK:
        sendPermissionDecision(approvalChoice ? "once" : "deny");
        responseSent = true;
        hw_leds_set_mode(approvalChoice ? LED_APPROVE_FLASH : LED_DENY_FLASH);
        break;
      default: break;
    }
  }

  // LED 模式
  if (!inPrompt) {
    hw_leds_set_mode(activeState == P_ATTENTION ? LED_ATTENTION_BREATH : LED_OFF);
  }
  hw_leds_tick();

  // 渲染
  if (buddyMode) buddyTick(activeState);
  else if (characterLoaded()) { characterSetState(activeState); characterTick(); }

  if (inPrompt) drawApproval(approvalHighlight);
  else          drawHudSimple();

  spr.pushSprite(0, 0);

  delay(16);
}
```

约 80 行（原版 `loop` 约 280 行）。

### 4.7 实装步骤（顺序执行，每步可独立烧录验证）

1. **新仓库 + 空 `main.cpp` 能编译能烧写**，串口输出 "hello"。验证 toolchain、partitions、board。
2. **加 `hw_display`**，屏幕亮，画 240×240 渐变，肉眼确认颜色不反色、不镜像。调 `TFT_eSPI` 色序参数直到和 X-Knob `hal/lcd.cpp` 表现一致。
3. **加 BLE**（原 `ble_bridge.cpp` 直接搬），用 Claude Desktop 配对，passkey 走串口打印，确认 bond 成功。
4. **加 `hw_input`**，串口打印所有旋钮事件，手动验证 detent 分辨率和按钮 click/double/long 判定。
5. **搬一个角色** `buddies/capybara.cpp`，改 `BUDDY_X_CENTER/BUDDY_Y_BASE` 到 240×240 中心，让它画出来且能动。
6. **接通 `dataPoll` + `derive` + 审批流**，收到 prompt 能 approve/deny，desktop 侧确认生效。

6 步跑完即 Phase 1 完成。

---

## 5. Phase 1 实现风险

1. **TFT_eSprite 240×240 内存分配**
   - 115 KB 16-bit sprite。TFT_eSPI 3.x 支持 `setAttribute(PSRAM_ENABLE, true)`；若用旧版，需手动 `ps_malloc`。
   - 第 2 步立即验证 pushSprite 帧率 > 30 Hz。刷不动则退路：8-bit 调色板 sprite（57 KB）或分块推。

2. **GC9A01 驱动色序 / 反色 / 偏移**
   - TFT_eSPI 的 GC9A01 驱动默认参数**几乎肯定和 X-Knob 面板不匹配**，需要逆向抄 X-Knob `hal/lcd.cpp` 的 init 序列到 TFT_eSPI 的 `TFT_INVERSION_ON` / `TFT_RGB_ORDER` / `TFT_COL_ORDER`。
   - 第 2 步必须通过才能继续。

3. **MT6701 事件速率 / 丢失**
   - 快速转动时跨过多个 detent 阈值，16 ms 主循环 `poll` 可能只报 1 个事件。
   - Phase 1：`hw_input_poll` **一次可返回多个事件**（累积未读）。若仍丢则 Phase 2 升级到 timer ISR 或独立 task。

4. **电机与 SimpleFOC 冲突**
   - Phase 1 **完全不跑 SimpleFOC**，open-loop 脉冲即可。代码 ~50 行。
   - 副作用：bump 感不如闭环精致。Phase 2 考虑接 SimpleFOC。

5. **PSRAM 是否实际可用**
   - X-Knob 上游 `platformio.ini` 未明确开 PSRAM。需要 `board_build.arduino.memory_type = qio_opi`（或 `qio_qspi`），视 PCB 上 PSRAM 芯片而定。
   - 第 1 步烧录后查 `ESP.getFreePsram()`；若 0 则 PCB 可能无 PSRAM，sprite 要走 8-bit 或分块。

6. **BLE 栈 + LittleFS + sprite 内存紧张**
   - BLE stack ~64 KB + LittleFS buffer + sprite + GIF 解码缓冲。
   - 先不优化，跑不起来再说。

## 6. 开放问题（实物验证时定）

- **MT6701 detent 分辨率**：初值 24/圈（15°/click），实际转起来调。
- **电机 bump 强度 / 时长**：初值 0.5 A × 30 ms，听声音 / 手感调。
- **NeoPixel attention 呼吸周期**：初值 1.2 s。
- **PSRAM 容量和启用方式**：烧录后验证。
- **X-Knob 上 pin 7 (`BATTERY_OFF_PIN`) 的具体语义**：读 X-Knob `hal/power.cpp` 后决定是否接管，若只是外部长按开关的副产品则不动。

---

## 7. Phase 2 预告（非本次实现范围）

Phase 1 跑通后再设计并实装：

- 上下文敏感输入状态机 `input_fsm`（DISP_NORMAL / INFO / PET / menu / settings / reset / prompt / screenOff / nap / passkey 各上下文的事件路由）
- 菜单 / 设置 / 重置三级面板
- Info 6 页、Pet 2 页
- 时钟模式（仅正向，删除横屏）
- nap（`BTN_LONG_3000` 进入/退出，电机长震/双 bump 反馈）
- dizzy（`ROT_FAST` = 100 ms 内 3+ detent）
- celebrate（level up 三下欢快 click）
- passkey 配对画面（圆屏居中大字 6 位数字）
- 深度睡眠唤醒（若需要）
- HUD 历史滚动翻页
- 字符包 ascii pet 切换 UI
- 边界 detent 硬 bump（菜单首尾再转"咔咔"）
- 电机升级到 SimpleFOC 闭环（可选）

Phase 2 每个功能加回来时做一次冒烟测试，顺序大致按原版 `main.cpp` 的结构：menu → settings → info → pet → clock → 一次性动效 → 手势。
