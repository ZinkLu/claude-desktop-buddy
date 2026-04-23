# D1 — 显示层重构 + SimpleFOC 集成实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将显示驱动从 TFT_eSPI 迁移到 GFX Library for Arduino，解决 SPI 冲突，引入 SimpleFOC 实现力反馈。

**Architecture:** 
- 显示层：GFX Library (GC9A01) + Canvas (240×240 16-bit framebuffer)，替代 TFT_eSPI + TFT_eSprite
- 电机控制：SimpleFOC 2.2.3 + GenericSensor (bitbang MT6701)，在 FreeRTOS task 中运行
- UI 层：所有绘制代码从 `TFT_eSprite&` 迁移到 `Arduino_GFX_Canvas*`

**Tech Stack:** 
- 移除：TFT_eSPI
- 添加：Arduino_GFX (v1.6.5), SimpleFOC (v2.2.3)

---

## 文件变更地图

| 文件 | 动作 | 说明 |
|------|------|------|
| `platformio.ini` | 修改 | 移除 TFT_eSPI，添加 Arduino_GFX + SimpleFOC |
| `src/hw_display.h` | 重写 | API 从 `TFT_eSprite&` 改为 `Arduino_GFX_Canvas*` |
| `src/hw_display.cpp` | 重写 | GFX GC9A01 init + Canvas framebuffer + flush |
| `src/hw_motor.h` | 修改 | 添加 SimpleFOC API (`hw_motor_init_foc`, `hw_motor_set_mode`) |
| `src/hw_motor.cpp` | 重写 | 保留现有开环代码 + 新增 SimpleFOC 闭环 |
| `src/main.cpp` | 修改 | 更新 sprite 引用，集成 FOC 模式切换 |
| `src/buddy.cpp` | 修改 | `TFT_eSprite*` → `Arduino_GFX_Canvas*` |
| `src/character.cpp` | 修改 | GIF 绘制适配新 canvas API |
| `src/clock_face.cpp` | 修改 | 时钟绘制适配 |
| `src/menu_panels.cpp` | 修改 | 菜单绘制适配 |
| `src/info_pages.cpp` | 修改 | 信息页绘制适配 |
| `src/pet_pages.cpp` | 修改 | 宠物页绘制适配 |

---

## Phase 0: 基础设施（验证显示驱动）

### Task 0.1: 更新 platformio.ini

**Files:**
- Modify: `platformio.ini`

**Steps:**
- [ ] **Step 1: 移除 TFT_eSPI，添加依赖**

```ini
lib_deps = 
    moononournation/GFX Library for Arduino@^1.6.5
    askuric/Simple FOC@^2.2.3
    ; ... 保留其他依赖
```

- [ ] **Step 2: 移除 TFT_eSPI 编译标志**

删除所有 `-DGC9A01_DRIVER`、`-DTFT_*`、`-DUSE_HSPI_PORT` 等标志。

- [ ] **Step 3: 编译验证**

Run: `~/.platformio/penv/bin/pio run`
Expected: 编译成功（TFT_eSPI 相关代码会报错，后续修复）

---

### Task 0.2: 重写 hw_display（最小可运行）

**Files:**
- Rewrite: `src/hw_display.h`
- Rewrite: `src/hw_display.cpp`

**Steps:**
- [ ] **Step 1: 实现 GFX GC9A01 初始化**

```cpp
#include <Arduino_GFX_Library.h>

static Arduino_DataBus *bus = new Arduino_HWSPI(14 /* DC */, 10 /* CS */);
static Arduino_GFX *gfx = new Arduino_GC9A01(bus, 9 /* RST */, 0 /* rotation */);
static Arduino_GFX_Canvas *canvas = new Arduino_GFX_Canvas(240, 240, gfx);

void hw_display_init() {
  gfx->begin();
  canvas->begin();
  hw_display_set_brightness(50);
}
```

- [ ] **Step 2: 实现 framebuffer flush**

```cpp
void hw_display_flush() {
  canvas->flush(); // 或 canvas->draw16bitBeRGBBitmap(...) 手动 flush
}
```

- [ ] **Step 3: 提供 canvas accessor**

```cpp
Arduino_GFX_Canvas* hw_display_canvas() { return canvas; }
```

- [ ] **Step 4: 编译验证**

Run: `~/.platformio/penv/bin/pio run`
Expected: 编译成功（hw_display 相关错误解决）

---

### Task 0.3: 最小主循环验证显示

**Files:**
- Modify: `src/main.cpp`（临时注释掉所有 UI，只显示纯色）

**Steps:**
- [ ] **Step 1: 最小绘制测试**

```cpp
void loop() {
  auto* canvas = hw_display_canvas();
  canvas->fillScreen(RGB565_BLACK);
  canvas->setCursor(50, 100);
  canvas->setTextColor(RGB565_WHITE);
  canvas->println("GFX OK!");
  hw_display_flush();
  delay(100);
}
```

- [ ] **Step 2: 硬件验证**

User flashes and reports: 屏幕是否显示 "GFX OK!"

---

## Phase 1: UI 代码迁移

### Task 1.1: 迁移 buddy.cpp（ASCII 宠物）

**Files:**
- Modify: `src/buddy.cpp`
- Modify: `src/buddy.h`

**Steps:**
- [ ] **Step 1: 更新函数签名**

```cpp
// 旧: void buddyRenderTo(TFT_eSprite* sp, PersonaState state, int scale);
// 新: void buddyRenderTo(Arduino_GFX_Canvas* canvas, PersonaState state, int scale);
```

- [ ] **Step 2: 替换所有 sprite API 调用**

TFT_eSPI API → GFX API 映射：
- `sp->drawPixel(x, y, color)` → `canvas->drawPixel(x, y, color)`
- `sp->setTextColor(fg, bg)` → `canvas->setTextColor(fg, bg)`
- `sp->setTextSize(n)` → `canvas->setTextSize(n)`
- `sp->drawString(str, x, y)` → `canvas->setCursor(x, y); canvas->print(str)`

- [ ] **Step 3: 编译验证**

- [ ] **Step 4: 硬件验证**

User: 检查 ASCII 宠物是否正常显示

---

### Task 1.2: 迁移 menu_panels.cpp

**Files:**
- Modify: `src/menu_panels.cpp`

**Steps:**
- [ ] **Step 1: 替换 sprite 引用为 canvas**
- [ ] **Step 2: 更新绘制 API（同上映射）**
- [ ] **Step 3: 编译 + 硬件验证**

---

### Task 1.3: 迁移 clock_face.cpp

**Files:**
- Modify: `src/clock_face.cpp`

**Steps：**
- [ ] **Step 1-3:** 同上模式

---

### Task 1.4: 迁移 info_pages.cpp & pet_pages.cpp

**Files:**
- Modify: `src/info_pages.cpp`
- Modify: `src/pet_pages.cpp`

**Steps：**
- [ ] **Step 1-3:** 同上模式

---

### Task 1.5: 迁移 character.cpp（GIF）

**Files:**
- Modify: `src/character.cpp`

**Steps：**
- [ ] **Step 1: 更新 GIF 绘制回调**

AnimatedGIF 库的绘制回调需要适配 GFX Canvas：
```cpp
// 旧: 直接操作 sprite buffer
// 新: 使用 canvas->drawPixel() 或批量写入
```

- [ ] **Step 2: 编译 + 硬件验证**

---

## Phase 2: SimpleFOC 集成

### Task 2.1: 实现 hw_motor_foc.cpp

**Files:**
- Create: `src/hw_motor_foc.cpp`
- Modify: `src/hw_motor.h`

**Steps:**
- [ ] **Step 1: 添加 SimpleFOC 初始化**

```cpp
#include <SimpleFOC.h>

BLDCMotor motor = BLDCMotor(7);
BLDCDriver3PWM driver = BLDCDriver3PWM(17, 16, 15);
GenericSensor sensor = GenericSensor(mt6701_read_rad, mt6701_init);

void hw_motor_init_foc() {
  sensor.init();
  motor.linkSensor(&sensor);
  driver.voltage_power_supply = 5;
  driver.init();
  motor.linkDriver(&driver);
  motor.controller = MotionControlType::torque;
  motor.PID_velocity.P = 2.3;
  motor.PID_velocity.D = 0.01;
  motor.voltage_limit = 5;
  motor.init();
  motor.initFOC(); // 自动寻相
}
```

- [ ] **Step 2: 实现 TaskMotorUpdate（基于原版 X-Knob）**

```cpp
void TaskMotorUpdate(void *pvParameters) {
  while(1) {
    sensor.update();
    motor.loopFOC();
    // ... 弹簧逻辑（同原版 X-Knob motor.cpp）
    vTaskDelay(1);
  }
}
```

- [ ] **Step 3: 编译验证**

---

### Task 2.2: 在 Home 页面集成边界弹簧

**Files:**
- Modify: `src/main.cpp`

**Steps:**
- [ ] **Step 1: 边界触发 FOC 弹簧**

```cpp
static void cb_on_scroll_edge(bool cw) {
  hw_motor_set_foc_mode(MOTOR_BOUNDARY_SPRING);
}

static void cb_on_hud_scroll_change(uint8_t) {
  hw_motor_set_foc_mode(MOTOR_NORMAL);
}
```

- [ ] **Step 2: 硬件验证力反馈**

User: 报告是否感受到"撞墙"阻力

---

## Phase 3: 收尾

### Task 3.1: 清理和优化

**Files:**
- 所有修改过的文件

**Steps：**
- [ ] **Step 1: 删除所有 TFT_eSPI 引用**
- [ ] **Step 2: 统一颜色常量（TFT_BLACK → RGB565_BLACK 等）**
- [ ] **Step 3: 最终编译 + 全功能测试**

---

## 风险评估

| 风险 | 概率 | 缓解措施 |
|------|------|----------|
| GFX Library 性能不如 TFT_eSPI | 中 | 使用 Canvas 减少 SPI 传输次数 |
| SimpleFOC initFOC() 仍然卡死 | 高 | 尝试手动提供 zero_electric_offset |
| UI 代码迁移遗漏 | 中 | 逐个文件验证，保留旧代码备份 |
| 字体显示异常 | 低 | GFX 支持 U8g2 字体，功能更强 |

## 协作模式

- **Claude:** 编写代码，编译验证，创建 PR
- **User:** 刷机测试，报告视觉/触觉反馈
- **Branch:** `phase-2d-gfx-refactor`
