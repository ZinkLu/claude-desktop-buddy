# D1 — 软件模拟弹簧扭矩设计文档

## 目标

在不引入 SimpleFOC 库的前提下，用纯软件实现类似 X-Knob 的弹簧-阻尼力反馈效果。

## 关键发现（来自 X-Knob 原版 motor.cpp）

- **极对数**: 7
- **PID**: P=1, I=0, D=0.01
- **电压限制**: 5V
- **位置宽度** (position_width): 8.2258° = 0.1435 rad
- **detent 强度**: 2.3
- **endstop 强度**: 1 (边界时 ×4)
- **snap_point**: 1.1 (超过 110% 位置宽度才切换档位)
- **死区**: min(20% × position_width, 1°)
- **速度限制**: |velocity| > 60 rad/s 时不输出扭矩

## 核心算法

### 1. 简化 FOC（不使用 SimpleFOC 库）

```
电角度 = 机械角度 × 7 (极对数)
目标电角度 = 电角度 + 90° × sign(扭矩)  // 90° 是最大扭矩角度
三相电压 = 幅度 × sin(目标电角度 + 相位差)
```

### 2. 弹簧-阻尼模型

```
torque = PID(-angle_to_center)
if |velocity| > 60: torque = 0  // 防飞车
out_of_bounds: PID.P *= 4  // 边界增强
```

### 3. 档位切换逻辑

```
if angle > position_width × snap_point:
    center += position_width
    position--
elif angle < -position_width × snap_point:
    center -= position_width
    position++
```

### 4. 死区处理

```
dead_zone = min(position_width × 0.2, 1°)
adjusted_angle = clamp(angle, -dead_zone, +dead_zone)
```

## API

```cpp
// 启用弹簧模式
void hw_motor_set_spring(float center_deg, float range_deg, 
                         float max_strength, float curve_exp=1.5);

// 禁用弹簧模式
void hw_motor_disable_spring();

// 在 hw_motor_tick() 内部自动调用
void hw_motor_spring_tick(uint32_t now_ms);
```

## 实现文件

- `src/hw_motor.cpp` — 添加弹簧逻辑
- `src/hw_motor.h` — 已有预留 API (`hw_motor_set_detents`)，改为 `hw_motor_set_spring`
- `src/main.cpp` — 在 `hw_motor_tick()` 中调用弹簧 tick

## 与原版的区别

1. **不使用 SimpleFOC 库** — 自己实现 FOC 核心公式
2. **不使用 FreeRTOS 任务** — 在主循环 `hw_motor_tick()` 中运行
3. **不使用硬件 SPI** — 继续使用 bitbang 读取 MT6701
4. **简化参数** — 只保留最核心的弹簧/阻尼/档位逻辑

## 参数调优建议

初始值从 X-Knob 原版提取：
- `position_width`: 8.2258°
- `detent_strength`: 2.3
- `endstop_strength`: 1.0 → 边界时 4.0
- `snap_point`: 1.1
- `dead_zone_percent`: 0.2
- `idle_correction_delay`: 500ms
- `idle_correction_max_angle`: 5°
- `idle_correction_rate`: 0.0005

用户可通过实验调整 `detent_strength` 改变手感硬度。
