# Phase 2 固件测试指南

> **Version**: 2026-04-22  
> **Branch**: `main`  
> **Hardware**: X-Knob (ESP32-S3 + GC9A01 240×240 LCD)  
> **关联文档**: [Phase 2 handoff](2026-04-19-phase2-handoff.md)

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

## 已知问题 / 待验证（与 handoff 同步）

### 硬件
- [ ] **设备拔 USB 断电** — 疑似电池或 MT3608 损坏。固件修复 `BATTERY_OFF`（commit `8c696a5`）未验证

### 电机 / 触觉（待 D1）
- [ ] **D1 SimpleFOC 闭环** — 当前开环脉冲"不细腻"，需升级为精细位置控制
- [ ] **Tickle 阈值** — 5 events/250ms 过严，正常旋转 4 Hz。需在 D1 后重新调优
- [ ] **Pet 问候/告别脉冲** — 3-pulse greeting（进入 Pet）/ 2-pulse bye（退出）未硬件验证

### 功能
- [x] **E2 CJK 字体** — ~~已搁置~~ ✅ 已实现：319 常用汉字，12×12 bitmap 字体（代码完成，待中文 prompt 场景验证）
- [ ] **D3 升级庆祝** — 代码已完成，需累积 50K tokens 硬件验证
- [ ] **F WiFi+NTP** — 未开始。需 WiFi 配网 UI + SNTP 客户端
- [ ] **G 深度睡眠** — 当前仅 PWM 变暗到 10%，未实现 LDO2 切断 / 深度睡眠 + EXT0 唤醒
- [ ] **D2 ROT_FAST** — 非 Pet 模式下快速旋转触发 dizzy，未实现
- [ ] **D2 菜单硬边界** — Menu/Settings/Reset 到达边界时 wall bump，未实现
- [ ] **D4 时钟 buddy 富化** — ❌ 已取消

---

## 测试结果记录

> **测试日期**: 2026-04-22
> **测试人员**: zinklu
> **固件版本**: main 分支最新

### Phase 2-A/B/C — 已完成并测试通过

| 模块 | 对应子项目 | 状态 | 备注 |
|------|-----------|------|------|
| **A. 基础交互** | A | ✅ 通过 | A2 短按触觉反馈已移除（符合当前设计） |
| **B. 菜单系统** | A | ✅ 通过 | 全部 7 项菜单功能正常 |
| **C. 时钟模式（基础）** | C | ✅ 通过 | 进入/显示/返回正常，无效时间显示 `--:--` |
| **F. Pet 模式** | B | ✅ 通过 | 抚摸/挠痒/挤压功能正常 |
| **H. HUD 滚动** | B | ✅ 通过 | 滚动、边界碰撞正常 |
| **D. 手动休眠** | D2 (partial) | ✅ 通过 | 3 秒休眠、唤醒、提示正常 |
| **E. 屏幕自动息屏** | G (partial) | ✅ 通过 | 30 秒息屏、唤醒、设置开关正常 |
| **I. 宠物选择器** | E (partial) | ✅ 通过 | 18 物种轮播、保存/取消/持久化正常 |
| **J. BLE / 配对** | A | ✅ 通过 | 配对、prompt、approve/deny 正常 |

### 代码已实现，待后续验证

| 模块 | 对应子项目 | 状态 | 备注 |
|------|-----------|------|------|
| **G. 升级庆祝** | D3 | ⏭️ 未验证 | 代码已完成（`statsPollLevelUp()`），需累积 50K tokens 触发 |
| **E2. CJK 字体** | E | ⏭️ 未测试 | 319 常用汉字已实现，待中文 prompt 场景验证 |
| **F. 信息页内容** | B | ⚠️ 未充分验证 | Info 页显示正常，但 CLAUDE live data / SYSTEM uptime+MAC / CREDITS 内容未逐项确认 |
| **F. Pet 问候/告别** | B | ⚠️ 未验证 | 3-pulse greeting（进入 Pet）/ 2-pulse bye（退出 Pet）电机反馈未确认 |

### 尚未实现（参照 handoff 文档）

| 模块 | 对应子项目 | 状态 | 说明 |
|------|-----------|------|------|
| **D1. SimpleFOC 电机闭环** | D1 | ⬜ 未开始 | 当前为开环脉冲，待升级为精细位置控制 |
| **D2. ROT_FAST 手势** | D2 | ⬜ 未实现 | 非 Pet 模式下快速旋转触发 dizzy + stats |
| **D2. 菜单硬边界** | D2 | ⬜ 未实现 | Menu/Settings/Reset 到达边界时触发 wall bump（当前仅 HUD 有） |
| **D4. 时钟 buddy 富化** | D4 | ❌ 已取消 | 用户确认取消（周五庆祝/周末 heart/深夜 dizzy 等） |
| **F. WiFi + NTP** | F | ⬜ 未开始 | 独立时间源，电源丢失后不再依赖 BLE 同步 |
| **G. 深度睡眠** | G | ⬜ 未实现 | 当前仅 PWM 变暗到 10%，未实现 LDO2 切断 / 深度睡眠 |
| **WiFi 配网 UI** | F | ⬜ 未开始 | SSID/密码输入界面 |

### 已知问题

| 问题 | 模块 | 严重程度 | 描述 |
|------|------|----------|------|
| **选择器布局未居中** | I (宠物选择器) | minor | 界面未居中，部分文字溢出屏幕（圆屏边缘裁剪） |
| **电机反馈不够细腻** | D1 (全局) | major | 用户反馈"不细腻"，需 SimpleFOC 闭环解决（handoff D1） |
| **Tickle 阈值过严** | F (Pet) | minor | 5 events/250ms ≈ 20 Hz，正常旋转 4 Hz，难以触发（handoff 遗留） |
| **设备拔 USB 断电** | 硬件 | major | 疑似电池或 MT3608 损坏，`BATTERY_OFF` 修复未验证（handoff） |

---

## 测试环境要求

1. **固件**: main 分支最新 (`git pull origin main`)
2. **BLE**: Claude Desktop 已连接（用于 prompt/时间同步测试）
3. **串口**: `pio device monitor` 保持打开，记录异常 log
4. **观察**: 注意屏幕显示完整性（round LCD 边缘裁剪）

**测试顺序建议（基础功能）**: A → B → I → C → D → E → F → G → H → J
**回归测试（后续迭代）**: E2 (CJK) → D3 (Celebrate) → Pet greeting/bye → Info 内容逐项
