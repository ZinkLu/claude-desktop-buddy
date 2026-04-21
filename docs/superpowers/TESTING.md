# Phase 2 固件测试指南

> **Version**: 2026-04-21  
> **Branch**: `main` (cf84505)  
> **Hardware**: X-Knob (ESP32-S3 + GC9A01 240×240 LCD)

## 快速刷入

```bash
cd /path/to/claude-desktop-buddy
git pull origin main
pio run -t upload -e x-knob
pio device monitor
```

---

## 功能测试清单

### A. 基础交互

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 旋转 | 左右转动旋钮 | 有触觉反馈（电机 bump）|
| 点击 | 短按按钮 | 有触觉反馈 |
| 长按 | 按住 600ms | 进入菜单（home 模式下）|
| 长按回主界面 | 菜单中按住 600ms | 返回 home |

### B. 菜单系统

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 菜单项 | LONG 进入菜单 | 显示 7 项：settings, clock, turn off, help, about, demo, close |
| 设置项 | 进入 settings | 显示 7 项：brightness, haptic, transcript, auto dim, ascii pet, reset, back |
| 亮度调节 | settings 中点击 brightness | 0→1→2→3→4→0 循环，屏幕亮度变化 |
| 触觉调节 | settings 中点击 haptic | 0→1→2→3→4→0 循环，每次有电机反馈 |
| 自动息屏 | settings 中点击 auto dim | on/off 切换 |
| 返回 | settings 中滚动到 back 点击 | 返回主菜单 |

### C. 时钟模式 (D4)

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 进入时钟 | home 下 LONG / menu 中点击 clock | 显示时间 HH:MM / :SS / Day Mon DD |
| 时钟 buddy | 进入时钟模式 | 下方显示 small buddy，根据时间变化状态：|
| | | - 01:00-07:00: 😴 sleep |
| | | - 周五 12:00+: 🎉 celebrate |
| | | - 周六/日: ❤️ heart |
| | | - 22:00+: 😵 dizzy |
| | | - 其他: 😐 idle |
| 时钟返回 | 时钟下 LONG | 返回 home |
| 无效时间 | 未同步 BLE 时间 | 显示 `--:--` + idle buddy |

### D. 手动休眠 (D2)

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 触发休眠 | home 下长按 3 秒（600ms 提示松手/继续，继续到 3 秒）| 屏幕变暗(10%)，buddy 显示 sleep |
| 唤醒 | 休眠中旋转或点击 | 恢复亮度，buddy 恢复 idle |
| 休眠菜单提示 | home 长按 600ms 未松手 | 顶部显示 "Release = Menu / Hold = Nap" |

### E. 屏幕自动息屏 (G)

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 自动息屏 | 30 秒无操作 | 亮度降至 10%，home/pet 显示 sleep buddy |
| 自动唤醒 | 息屏后旋转/点击 | 立即恢复用户设置亮度 |
| 设置开关 | settings 关闭 auto dim | 不再自动息屏 |
| 各模式 | menu/settings/clock 下 | 只降低亮度，不黑屏 |

### F. Pet 模式 (B)

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 进入 Pet | home 下 CLICK | 显示 pet 主界面（mood, Lv, fed, energy, tokens）|
| 抚摸 | Pet 中旋转 | 电机 purr 震动，显示 hearts |
| 挠痒 | Pet 中快速同向旋转 5 次/250ms | 3-pulse annoyed buzz + dizzy 动画 |
| 挤压 | Pet 中长按 | squish 震动 + heart 动画 |
| 返回 | Pet 中 CLICK | 进入时钟 |
| 信息页 | menu 中 help/about | 显示 ABOUT/CLAUDE/SYSTEM/CREDITS（可旋转切换）|

### G. 升级庆祝 (D3)

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 触发庆祝 | 累积 50K tokens（需 BLE 连接）| 3 秒 celebrate 动画 + 电机 wiggle + pulse series |
| 只触发一次 | 升级后 | 不会重复触发直到下一级 |

### H. HUD 滚动 (B)

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 滚动历史 | home 下旋转 | 底部橙色指示器移动，显示历史 transcript |
| 边界碰撞 | 滚到最旧/最新 | 电机反向脉冲（wall bump）|
| 空历史 | 无 transcript | 指示器不显示 |

### I. 宠物选择器 (E1)

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 进入选择器 | settings → ascii pet → CLICK | 显示 "Choose Your Buddy" + 当前物种动画 |
| 浏览物种 | 选择器中旋转 | 切换物种，实时预览 idle 动画，电机 bump |
| 确认 | 选择器中 CLICK | 保存到 NVS，返回 settings，显示新物种名 |
| 取消 | 选择器中 LONG | 不保存，返回 settings，保持原物种 |
| 持久化 | 重启设备 | 自动恢复上次选择的物种 |
| 全部物种 | 浏览所有 18 个 | 每个都能正常显示动画：capybara, axolotl, blob, cactus, cat, chonk, dragon, duck, ghost, goose, mushroom, octopus, owl, penguin, rabbit, robot, snail, turtle |

### E2. CJK 字体支持

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 中文 prompt | Claude Desktop 发送含中文的 prompt | 正确显示汉字，无 `??` |
| 混合文本 | prompt 含中英文混合 | 中英文均正确显示，对齐正常 |
| HUD 中文 | 转录记录含中文 | HUD 换行正确，无乱码 |
| 缺字占位 | 收到字符清单外的汉字 | 显示 `□` 占位符，不崩溃 |
| ASCII 兼容 | 英文 prompt / 菜单 | 与之前完全一致 |
| 缓冲区 | 超长中文 prompt (>16 字) | 安全截断，不溢出 |

### J. BLE / 配对

| 测试项 | 操作 | 预期结果 |
|--------|------|----------|
| 配对界面 | 首次配对 | 显示 6 位 passkey |
| 提示到达 | Claude Desktop 发送 prompt | 自动返回 home，显示 approval 界面 |
| 批准/拒绝 | 旋转选择，点击确认 | 发送权限决定，显示下一个 prompt |

---

## 问题报告格式

发现 bug 时请按以下格式记录：

```
**模块**: [A/B/C/D/E/F/G 等]
**测试项**: [具体测试项]
**复现步骤**:
1. ...
2. ...
**预期结果**: ...
**实际结果**: ...
**串口日志**:
```
[如有相关 log 贴这里]
```
**严重程度**: [blocker / major / minor]
```

---

## 已知问题 / 待验证

- [ ] **D1 SimpleFOC**: 电机闭环未实现，当前触觉为开环脉冲
- [x] **E2 CJK 字体**: ~~中文 prompt 显示为 `??`，已搁置~~ ✅ 已实现：319 常用汉字，12×12 bitmap 字体
- [ ] **F WiFi+NTP**: 未开始
- [ ] **Phase 2-B 遗留**: Info 页内容（CLAUDE live data, SYSTEM uptime/MAC, CREDITS）、Pet 3-pulse greeting/2-pulse bye、30-s "fell asleep" 转换 —— 上次 session 未硬件验证

---

## 测试环境要求

1. **固件**: main 分支最新 (`git pull origin main`)
2. **BLE**: Claude Desktop 已连接（用于 prompt/时间同步测试）
3. **串口**: `pio device monitor` 保持打开，记录异常 log
4. **观察**: 注意屏幕显示完整性（round LCD 边缘裁剪）

**测试顺序建议**: A → B → I → C → D → E → F → G → H → J
（先验证基础交互，再逐个功能模块）
