# E2 CJK 字体支持设计文档

> **Version**: 2026-04-21
> **Scope**: Phase 2 Sub-project E2
> **Status**: Approved for implementation

---

## 1. 目标

解决 Claude Desktop 中文提示在设备上显示为 `??` 或空白的问题。实现精简 CJK 字体渲染，优先覆盖 prompt + HUD 场景。

---

## 2. 当前状态分析

### 2.1 渲染系统

- **库**: TFT_eSPI v2.5.43，使用内置 GLCD/Font2/Font4 位图字体
- **显示**: GC9A01 240×240 圆形 LCD，16bpp，HSPI @ 40MHz
- **缓冲区**: 240×240 精灵缓冲区（~115KB PSRAM）
- **字体大小**:
  - `setTextSize(1)` = 6×8 px（HUD 使用）
  - `setTextSize(2)` = 12×16 px（promptTool 使用）

### 2.2 问题诊断

| 问题 | 影响 |
|------|------|
| 无 UTF-8 处理 | `strlen()` 计算字节而非字符数，CJK 截断错误 |
| 无 CJK 字形 | TFT_eSPI 内置字体仅含 ASCII，CJK 码点显示为 `??` |
| `wrapInto()` 字节截断 | 多字节 UTF-8 序列在字节边界截断，产生乱码 |
| 缓冲区按 ASCII 设计 | `promptTool[20]` 最多容纳 6-7 个 CJK 字符 |

---

## 3. 设计决策

### 3.1 字符集范围：~350 常用汉字

覆盖 Claude Desktop 中文提示高频场景：
- **工具名称**: 文件、编辑、搜索、浏览器、终端、代码、运行、调试
- **操作动词**: 打开、关闭、保存、读取、写入、执行、撤销、重做
- **状态描述**: 完成、失败、等待、确认、取消、继续、返回
- **系统词汇**: 设置、帮助、关于、亮度、触觉、转录、宠物、时钟
- **常用虚词**: 的、是、在、有、我、你、他、她、它、了、着、过

扩展机制：通过 `tools/cjk_chars.txt` 维护，运行生成脚本即可更新。

### 3.2 字形规格：12×12 点阵

| 属性 | 值 | 理由 |
|------|-----|------|
| 尺寸 | 12×12 px | 匹配 `setTextSize(1)` 行高 12px，HUD 一行刚好容纳 |
| 存储 | 每字 18 bytes (12×12/8) | 紧凑，350 字约 6.3KB |
| 颜色 | 单色（1bpp） | 运行时按前景色渲染，与现有文本一致 |
| 对齐 | 顶部对齐 + 1px 行间距 | 视觉居中，避免粘连 |

### 3.3 渲染策略：混合渲染

```
输入字符串: "approve? 打开文件"
               ↓
         UTF-8 解码器
               ↓
    codepoint < 0x80? ──Yes──→ TFT_eSPI 内置字体 (Font 2/4)
               │ No
               ↓
    二分查找 glyph_index ──Found──→ 12×12 bitmap blit
               │ Not Found
               ↓
           显示 "□" 占位符
```

**优势**：
- ASCII 继续走优化过的 TFT_eSPI 路径，零性能损失
- CJK 走独立路径，不修改 TFT_eSPI 内部
- 混合渲染时自动对齐基线

---

## 4. 架构设计

### 4.1 文件结构

```
src/
├── font_cjk.h          # 生成文件：字形 bitmap + 索引表
├── font_cjk.cpp        # 渲染引擎：UTF-8 解码 + 混合渲染
├── font_cjk_generator.h # 生成器配置（字符清单、字体参数）
└── ...

tools/
├── generate_cjk_font.py  # Python 脚本：TTF → font_cjk.h
└── cjk_chars.txt         # 字符清单（每行一个汉字，UTF-8）

assets/
└── fonts/
    └── source_han_sans.ttf  # 源字体（思源黑体或 Noto Sans CJK）
```

### 4.2 核心 API

```cpp
// font_cjk.h / font_cjk.cpp

// 初始化（主函数中调用一次）
void cjk_font_init();

// 设置渲染目标（精灵或 TFT）
void cjk_set_target(TFT_eSprite* sprite);

// 渲染 UTF-8 字符串（混合渲染）
// x, y: 左上角坐标（像素）
// color: RGB565 前景色
// bg: RGB565 背景色（TFT_BLACK 表示透明/不填充）
// font_size: 1=小字(HUD), 2=大字(prompt)
void cjk_draw_string(int x, int y, const char* utf8_str, 
                     uint16_t color, uint16_t bg, uint8_t font_size);

// 计算字符串像素宽度（用于居中/右对齐）
int cjk_text_width(const char* utf8_str, uint8_t font_size);

// 统计 UTF-8 字符串的字符数（codepoint 数）
int cjk_utf8_strlen(const char* utf8_str);
```

### 4.3 数据格式

**Glyph 表（只读，PROGMEM）**:
```cpp
// 排序的 Unicode codepoint 数组（用于二分查找）
static const uint32_t CJK_CODEPOINTS[] PROGMEM = {
  0x4E00, 0x4E01, 0x4E03, ...  // ~350 个 codepoint
};

// 对应的 12×12 bitmap 数组
static const uint8_t CJK_BITMAPS[][18] PROGMEM = {
  {0x00, 0x00, ...},  // 0x4E00 (一)
  {0x00, 0x00, ...},  // 0x4E01 (丁)
  ...
};
```

**索引查找**（O(log n)，n=350 → 9 次比较）:
```cpp
static int lookup_glyph(uint32_t cp) {
  int lo = 0, hi = CJK_GLYPH_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t mid_cp = pgm_read_dword(&CJK_CODEPOINTS[mid]);
    if (mid_cp == cp) return mid;
    if (mid_cp < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}
```

---

## 5. 集成修改点

### 5.1 新增文件

| 文件 | 说明 |
|------|------|
| `src/font_cjk.h` | 生成的字形数据（~7KB） |
| `src/font_cjk.cpp` | 渲染引擎（~300 行） |
| `tools/generate_cjk_font.py` | 字体生成脚本（~150 行） |
| `tools/cjk_chars.txt` | 字符清单（~350 行） |
| `docs/superpowers/specs/2026-04-21-cjk-font-design.md` | 本设计文档 |

### 5.2 修改文件

| 文件 | 修改内容 | 行号范围 |
|------|----------|----------|
| `src/main.cpp` | `drawApproval()`: promptTool 渲染改用 `cjk_draw_string()` | ~174-204 |
| `src/main.cpp` | `drawHudSimple()`: HUD 行渲染改用 `cjk_draw_string()` | ~239-280 |
| `src/main.cpp` | `wrapInto()`: 重写为 UTF-8 aware | ~209-237 |
| `src/data.h` | 评估并扩大 `promptTool`/`promptHint` 缓冲区 | ~15-20 |
| `platformio.ini` | 确保 `SMOOTH_FONT=1` 保留（未来扩展），无其他改动 | ~45 |

### 5.3 UTF-8 文本换行算法

重写 `wrapInto()`，以 **codepoint** 为单位计算：

```cpp
static uint8_t wrapIntoUtf8(const char* in, char out[][48], uint8_t maxRows, uint8_t maxCols) {
  // maxCols = 每行最大字符数（CJK=1字，ASCII=1字）
  // 按 codepoint 遍历，遇到空格尝试换行
  // 如果单词长度 > maxCols，在字符边界硬截断（避免字节截断）
}
```

关键区别：
- 输入仍是 UTF-8 字节流，但解析时识别完整的 codepoint
- 宽度计算按 codepoint 数（CJK=1 宽，ASCII=1 宽）
- 截断点在 codepoint 边界，绝不在 UTF-8 多字节中间

---

## 6. 构建流程

### 6.1 首次设置

```bash
# 1. 安装依赖
pip install pillow

# 2. 准备源字体（或自动下载 Noto Sans CJK）
# 字体文件放 assets/fonts/

# 3. 编辑字符清单
echo "的" >> tools/cjk_chars.txt
echo "是" >> tools/cjk_chars.txt
# ... （初始清单已提供 ~350 字）

# 4. 生成字体头文件
python tools/generate_cjk_font.py \
  --chars tools/cjk_chars.txt \
  --font assets/fonts/NotoSansCJK-Regular.otf \
  --size 12 \
  --out src/font_cjk.h
```

### 6.2 日常开发

开发者修改 `tools/cjk_chars.txt` 后，重新运行生成脚本，然后 `pio run`。

**CI/CD 考虑**：生成脚本可在构建前自动运行（可选）。

---

## 7. 性能预估

| 指标 | 数值 | 说明 |
|------|------|------|
| ROM 占用 | ~7KB | 350 字 × 18 bytes |
| RAM 占用 | ~0B | 全部放 PROGMEM（Flash），运行时不加载 |
| 渲染速度 | ~50 μs/字 | 二分查找 + 12×12 位图 blit |
| 字符串 "打开文件" | ~200 μs | 4 个 CJK 字 |
| 对比 ASCII | 同量级 | ASCII 走 TFT_eSPI 硬件优化路径，更快 |

---

## 8. 风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| 350 字不够用，出现 `□` | 定期收集未覆盖字符，扩展清单后重新生成 |
| 12×12 在小屏上不够清晰 | 备选 16×16 方案（存储增至 ~12KB），可随时切换 |
| 源字体版权问题 | 使用 OFL 许可的 Noto Sans CJK / 文泉驿 |
| 生成脚本依赖 Pillow | 脚本仅开发时运行，设备端零依赖 |
| wrapInto 重写引入 bug | 保留原函数为 `wrapIntoAscii()`，并行对比测试 |

---

## 9. 验证清单

实施后需验证：

- [ ] 中文 prompt "文件编辑器打开" 正确显示，无 `??`
- [ ] HUD 转录文本含中文时换行正确，无乱码
- [ ] ASCII 文本渲染不受影响（英文 prompt、菜单）
- [ ] 混合文本 "approve? 打开" 正确渲染中英文
- [ ] 缓冲区溢出测试：超长 CJK 文本安全截断
- [ ] 不在字符清单中的字显示 `□` 占位符，不崩溃
- [ ] 固件编译通过，`pio run` 无警告
- [ ] 内存占用增加 < 10KB（对比之前固件）

---

## 10. 未来扩展

| 扩展 | 方案 | 工作量 |
|------|------|--------|
| 支持 3000 字 | 扩展 `cjk_chars.txt`，重新生成 | 30 分钟 |
| 16×16 清晰字形 | 修改 `--size 16`，重新生成 | 10 分钟 |
| 菜单/设置中文化 | 替换 `menu_panels.cpp`/`info_pages.cpp` 渲染调用 | 2 小时 |
| 粗体/斜体 | 生成第二套 bitmap，加粗算法处理 | 半天 |
| 完整 GB2312 | ~6763 字，ROM 增至 ~120KB，仍可接受 | 1 小时 |

---

## 11. 相关文档

- `docs/superpowers/2026-04-19-phase2-handoff.md` — Phase 2 总体进度
- `docs/superpowers/TESTING.md` — 测试清单（需添加 E2 验证项）
- `docs/superpowers/specs/2026-04-21-cjk-font-design.md` — 本文件

---

*End of document*
