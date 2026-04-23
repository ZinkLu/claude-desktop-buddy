# D1 — SimpleFOC 力反馈集成状态 (2026-04-23)

## 已完成 ✅

### 1. 显示层重构
- **TFT_eSPI → GFX Library for Arduino 1.2.1**
- 修复了 MISO=13 与背光共享引脚的问题
- 正确的初始化顺序：`gfx->begin()` → 背光 PWM
- 所有 UI 模块已迁移：buddy, character, clock, menu, info, pet, font_cjk

### 2. 电机控制重构
- **SimpleFOC 2.2.3 集成**（编译通过）
- 使用 HSPI 读取 MT6701，避免与显示 SPI 冲突
- 实现了基于 X-Knob 原版的弹簧-档位力反馈：
  - 极对数 = 7
  - PID: P=2.3, D=0.01
  - 1kHz 更新频率
  - 反转旋转方向（匹配 X-Knob 硬件）
  - Idle correction（静止时缓慢修正中心点）
  - 速度保护（>60 rad/s 切断力矩）

### 3. 输入模块适配
- 输入模块不再直接读取 MT6701
- 从 FOC 任务获取角度 `hw_motor_foc_angle_deg()`
- 避免了 SPI/bitbang 竞争冲突

### 4. 电机特效 FOC 化
所有开环脉冲效果已替换为 FOC torque 效果：
- `hw_motor_click()` — 30ms 正弦扭矩脉冲
- `hw_motor_kick()` — 40ms 方向性推力
- `hw_motor_wiggle()` — L-R-L 三段脉冲
- `hw_motor_purr_start/stop()` — 150ms 交替方向连续振动
- `hw_motor_vibrate()` — 40ms 高频交替振动
- `hw_motor_pulse_series()` — 间隙脉冲序列

**实现机制**：Core 0（主循环）通过 volatile 结构体发送效果请求，Core 1（电机任务）在 1kHz 循环中读取并叠加到弹簧力矩上。One-shot 效果激活时暂时禁用弹簧，确保效果不被抵消。

---

## 已知问题 ❌

### 问题 1: Pet 模式电机特效无法感知（严重）
**现象**：进入 Pet 模式、抚摸、tickle、长按、退出 Pet 时，没有任何电机反馈。

**已尝试的修复**：
1. ✅ 增大效果强度（3V → 5V）
2. ✅ One-shot 效果激活时禁用弹簧（防止抵消）
3. ✅ 效果与弹簧扭矩分离计算

**仍失败的原因（待排查）**：
- 效果触发机制可能有问题（`_effect_trigger` volatile 标志是否被正确读取？）
- 效果计算可能返回 0（时间窗口已过或参数错误）
- 可能是 `motor.move()` 的 torque 值太小（需要验证实际电压输出）
- 需要加串口调试打印，确认效果是否被触发和计算值

**调试建议**：
```cpp
// 在 TaskMotorUpdate 的 effect 计算后添加：
Serial.printf("[effect] type=%d torque=%.2f spring=%.2f\n", 
              _active_effect.type, effect_torque, spring_torque);
```

### 问题 2: 档位感强度（次要）
**现象**：力反馈有回弹感，但"咔嗒"档位感不够明显。

**可能原因**：
- PID 参数需要调优（当前 P=2.3，原项目用 P=2.3*4=9.2）
- 或者档位间隔太宽（8.22°）
- 或者电压限制 5V 不够（原项目也是 5V）

**建议**：先解决 Pet 效果问题，再调档位感。

---

## 下一步工作 📋

### 高优先级
- [ ] **调试 Pet 效果**：加串口日志，确认效果触发和计算值
- [ ] **验证效果是否到达电机**：测量 `motor.move()` 的实际输出电压
- [ ] **对比原项目**：用 X-Knob 原项目的电机控制代码直接替换测试

### 中优先级
- [ ] **调优档位感**：调整 PID P 增益或档位宽度
- [ ] **测试所有效果**：
  - 菜单点击反馈
  - 边界提示（HUD scroll edge）
  - 庆祝动画（wiggle + pulse）
  - Passkey 输入反馈

### 低优先级
- [ ] **清理代码**：删除 `hw_motor.cpp`（已删除，确认无残留）
- [ ] **优化内存**：检查 FOC 任务的栈使用（当前 4096 bytes）

---

## 分支信息

- **分支**: `phase-2d`
- **基于**: `main`
- **修改文件**:
  - `platformio.ini` — 添加 GFX + SimpleFOC 依赖
  - `src/hw_display.{h,cpp}` — GFX 显示驱动
  - `src/hw_motor.h` — 更新 API（添加 FOC 接口）
  - `src/hw_motor_foc.cpp` — 新增 SimpleFOC 闭环控制
  - `src/hw_input.cpp` — 从 FOC 读取角度
  - `src/main.cpp` — 初始化 FOC，调用新 API
  - `src/buddy.cpp/h` — GFX 迁移
  - `src/character.cpp/h` — GFX 迁移
  - `src/clock_face.cpp` — GFX 迁移
  - `src/menu_panels.cpp` — GFX 迁移
  - `src/info_pages.cpp` — GFX 迁移
  - `src/pet_pages.cpp` — GFX 迁移
  - `src/font_cjk.cpp/h` — GFX 迁移
- **删除文件**:
  - `src/hw_motor.cpp` — 旧的开环电机控制

---

## 协作记录

**Claude**:
- 编写所有代码并编译验证
- 无法实际测试硬件手感
- 需要用户反馈测试效果

**用户**:
- 负责刷机到 X-Knob 设备
- 报告手感和行为
- 当前反馈：基本档位感正常，但 Pet 特效丢失

**下次会话入口**:
1. 检查本文档的问题列表
2. 从 `phase-2d` 分支继续
3. 优先解决 Pet 效果问题
