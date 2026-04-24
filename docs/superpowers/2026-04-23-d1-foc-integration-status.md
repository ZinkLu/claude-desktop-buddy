# D1 — SimpleFOC 力反馈集成状态 (2026-04-24)

## 已完成 ✅

### 1. 显示层重构
- **TFT_eSPI → GFX Library for Arduino 1.2.1**
- 修复了 MISO=13 与背光共享引脚的问题
- 正确的初始化顺序：`gfx->begin()` → 背光 PWM
- 所有 UI 模块已迁移：buddy, character, clock, menu, info, pet, font_cjk

### 2. 电机控制重构
- **SimpleFOC 2.2.3 集成**（编译通过，硬件验证手感细腻）
- 使用 HSPI 读取 MT6701，避免与显示 SPI 冲突
- 基于 X-Knob 原版的弹簧-档位力反馈：
  - 极对数 = 7
  - PID: P=2.3×4=9.2（受 haptic 设置缩放）, D=0.01
  - 1kHz 更新频率
  - 反转旋转方向（匹配 X-Knob 硬件）
  - Idle correction（静止时缓慢修正中心点）
  - 速度保护（>60 rad/s 切断力矩）

### 3. 输入模块适配
- FOC 模式下使用 **position-based 旋转检测**（`hw_motor_position()`）
- 电机任务维护 `_position` 计数器，每跨一个 snap point ±1
- 彻底消除了弹簧回弹/idle correction 产生的假旋转事件
- Pre-FOC 阶段仍用 bitbang MT6701 + 15° 阈值作为 fallback

### 4. 电机特效系统
效果通过 **lock-free SPSC 环形缓冲区**（8 槽）从 Core 0 发送到 Core 1：
- `hw_motor_click()` — 30ms balanced 正弦脉冲（push-pull，净零漂移）
- `hw_motor_wiggle()` — L-R-L 三段脉冲 (~220ms)
- `hw_motor_vibrate()` — 连续交替振动（25Hz）
- `hw_motor_pulse_series()` — 间隙脉冲序列

Purr 使用**独立 volatile 通道**，不经过效果队列：
- `hw_motor_purr_start/stop()` — 150ms 节拍交替振动
- 可随时启停，不被一次性效果覆盖

所有效果**始终叠加在弹簧上**（`motor.move(spring + effect + purr)`），弹簧永不禁用。

### 5. Haptic 等级联动
- `hw_motor_set_haptic(level)` 控制弹簧档位强度
- haptic 0 = 自由旋转（P=0），haptic 4 = 原始 X-Knob 手感（P=9.2）
- 启动时从 NVS 同步，settings 切换时实时更新
- 效果强度由各调用点传入 `settings().haptic`

### 6. 代码清理
- **hw_motor.h**: 69 行 → 39 行，删除 11 个未使用的 legacy API
- **hw_motor_foc.cpp**: 544 行 → ~300 行，删除 legacy spring 空壳、`EFFECT_KICK`
- 删除 `hw_motor_tick()`（no-op）及其在 main.cpp 中的调用

---

## 已修复的问题

| 问题 | 根因 | 修复 |
|------|------|------|
| Pet 效果完全无感 | PURR 的 `duration_ms=0xFFFFFFFF` 导致 uint32_t 溢出，立即过期 | PURR 改为独立通道，无过期检查 |
| PULSE_SERIES 最后一个脉冲丢失 | `remaining--` 在脉冲播放前执行，remaining=0 时立即退出 | 重构为先播放再递减 |
| 电机单向漂移 | CLICK 为单向正弦 + 效果激活时禁用弹簧 → 正反馈循环 | CLICK 改为 balanced push-pull；所有效果叠加在弹簧上 |
| PURR 触发无限反馈循环 | PURR 物理振动 → 假旋转事件 → 刷新 idle timer → PURR 永不停止 | Pet 回调在 `hw_motor_effect_active()` 时忽略旋转 |
| PULSE_SERIES 触发 tickle 循环 | 效果移动轴杆 → 快速 CW → tickle 检测 → 更多 pulse_series | 同上，通用 effect_active 过滤 |
| 电机抽搐/CW 刷屏 | PID D=0.5（原项目 D=0.01）+ raw angle 编码器拾取弹簧运动 | D 改回 0.01；FOC 模式用 position-based 旋转检测 |
| Haptic 设置不影响档位感 | 弹簧 P 增益硬编码 2.3×4 | 按 haptic level 缩放 P（0/25%/50%/75%/100%） |

---

## 当前状态

**硬件验证结果**：
- ✅ 电机手感细腻，档位感正常
- ✅ 旋转不再产生假事件
- ✅ Pet 效果可感知（purr/pulse_series/vibrate）
- ✅ Haptic 设置生效

**待测试**：
- [ ] Haptic 0-4 各级档位感差异
- [ ] Pet 完整流程：入场脉冲 → 抚摸 purr → tickle buzz → 挤压 vibrate → 退场脉冲
- [ ] 庆祝动画（wiggle + pulse_series）
- [ ] HUD 边界碰撞反馈

---

## 分支信息

- **分支**: `phase-2d`
- **基于**: `main`
- **核心修改文件**:
  - `src/hw_motor.h` — 精简后的 FOC 电机 API
  - `src/hw_motor_foc.cpp` — SimpleFOC 闭环控制 + 效果系统
  - `src/hw_input.cpp` — position-based 旋转检测（FOC 模式）
  - `src/main.cpp` — haptic 联动、效果调用、删除 hw_motor_tick

## 协作记录

**Claude**: 编写代码、编译验证、分析 bug 根因
**用户**: 刷机测试、报告手感和行为、确认修复效果
