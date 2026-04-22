# Phase 2 handoff — backlog + collaboration pattern

Written 2026-04-19 at the end of the Phase 2-B session. Phase 2 is decomposed into sub-projects A / B / C / D / E / F / G. A / B / C shipped. D / E / F / G remain. Hardware-level issues are tracked separately at the bottom.

## Collaboration pattern (established over Phase 1 + Phase 2 A/B/C)

The user and Claude split the loop as follows:

- **Claude writes code and runs all builds/tests**:
  - `~/.platformio/penv/bin/pio run` (firmware, `[env:x-knob]`)
  - `~/.platformio/penv/bin/pio test -e native` (Unity host tests)
  - No hardware flashing. Claude reports `[SUCCESS]` / test counts from the build output.
- **User handles hardware**:
  - `pio run -t upload && pio device monitor`
  - Reports serial log output when something is off.
  - Reports visual / tactile behavior.
- **Claude creates PRs** via `gh` CLI (authenticated as ZinkLu). User reviews and merges via GitHub UI.
- **Claude commits and pushes** to whichever feature branch is active. After merge, Claude pulls `main` locally.

Workflow skills to invoke when starting a new sub-project, in order:

1. `superpowers:brainstorming` — scope, decisions, spec
2. `superpowers:writing-plans` — plan doc at `docs/superpowers/plans/YYYY-MM-DD-<topic>.md`
3. `superpowers:subagent-driven-development` — dispatch per-task subagents, Claude reviews between

Branch naming: `phase-2d`, `phase-2e`, etc., cut from `main`. PRs via `https://github.com/ZinkLu/claude-desktop-buddy/pull/new/<branch>`.

## Platform notes to remember next session

- Board: ESP32-S3 DevKitC-1 (X-Knob hardware), 16 MB flash, PSRAM enabled (`qio_opi`).
- Display: GC9A01 240×240 round LCD on HSPI via TFT_eSPI. Must set `USE_HSPI_PORT=1` or VSPI-default crashes on S3.
- Rotary encoder: MT6701 bitbanged (SCLK=2, MISO=1, SS=42) — NOT on Arduino's SPIClass. Using SPIClass(FSPI) on S3 silently breaks the LCD on HSPI.
- LEDC channels: 0 = TFT backlight PWM (5 kHz); 2, 4, 6 = motor phases (20 kHz). Each channel must use a unique timer (`timer = channel / 2` on Arduino-ESP32), so motor phases can't squat on ch 1/3/5.
- USB CDC: `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` required or Serial prints go to UART0 which is not wired on X-Knob.
- `stats.h` / `data.h` / `xfer.h` are header-only with file-scope static state. ONLY include them from `src/main.cpp`. Any other .cpp that needs a setting must use an `extern` bridge function defined in main.cpp (examples: `current_haptic_level()`, `panel_brightness()`, `info_bt_name()`, `pet_level()` etc.).
- `gh` CLI is authenticated as the correct GitHub user — PRs are created automatically by Claude.

## Completed sub-projects

- **C — clock mode** (PR #2, commit `c8a5288`, 2026-04-18). LONG toggles home↔clock; menu has a `clock` entry that's the canonical path now. Valid-time layout HH:MM / :SS / Day Mon DD. Invalid-time (no BLE sync) shows `--:--` placeholder. Prompt arrival preempts. Spec: `docs/superpowers/specs/2026-04-18-phase2c-clock-design.md`.

- **A — input FSM + menus + passkey UI** (PR #3, commit `1f50775`, 2026-04-18). Context-sensitive LONG routes home→menu / menu→home / clock→home. Menu items: settings, clock, turn off, help, about, demo, close. Settings: brightness (0-4 PWM), haptic (0-4 motor strength), transcript toggle, reset submenu, back. Reset 3-item double-confirm (3 s window). Passkey full-screen during BLE pairing. `hw_motor_click_default()` wrapper. `hw_power_off()` wakes on EXT0 (push button pin 5). Spec: `docs/superpowers/specs/2026-04-18-phase2a-menus-design.md`.

- **B — Info pages + Pet mode + HUD scroll** (PR #4, commit `6932cba`, 2026-04-19). 4 Info pages (ABOUT/CLAUDE/SYSTEM/CREDITS) reached via menu help/about or (from home) CLICK → Pet → CLICK → Clock → CLICK → home. Pet is interactive: any rotation = stroke (purr motor loop + hearts), 5 same-direction in 250 ms = tickle (3-pulse annoyed buzz + dizzy), long-press = squish vibration. All stats on one page (mood hearts, Lv, fed meter, energy bars, approved / denied, nap, tokens total+today). HUD scroll with motor edge bump (no visual `-N`). Character shifted up (BUDDY_Y_BASE=40) to free the lower 60 px. Spec: `docs/superpowers/specs/2026-04-18-phase2b-info-pet-hud-design.md`. Plan: `docs/superpowers/plans/2026-04-18-phase2b-info-pet-hud.md`.

## Phase 2-B polish backlog (to absorb into D1)

These surfaced during B's hardware verification but were deferred:

- **Haptic not subtle enough** — user called the current open-loop pulses "不细腻". Real cure is SimpleFOC closed loop in D1.
- **Tickle threshold strict** — 5 events in 250 ms ≈ 20 Hz. Normal rotation produces 4 Hz. User reverted an earlier loosening attempt (`9786213` → reverted in `eb22941`). Revisit during D1 with better motor feedback.
- **Not yet verified on device** — Info pages content (CLAUDE live data, SYSTEM uptime/MAC, CREDITS), Pet 3-pulse greeting / 2-pulse bye, 30-s "fell asleep" transition, menu help → Info page 0 / about → Info page 3. Treat as smoke tests at the start of the next hardware session, not blockers.

## Sub-project D — gestures + effects + motor upgrade (active next)

Decomposition:

- **D1 — Motor closed-loop upgrade** (SimpleFOC). Replaces open-loop torque pulses with fine positional control. Refines every existing haptic effect (detent clicks, purr, tickle buzz, edge bump, squish, greeting pulses). Tickle threshold + purr debounce can then be tuned sensibly. Also absorbs the Phase 2-B polish backlog above.
- **D2 — New gestures**. `BTN_LONG_3000` → manual nap (face-down analog). `ROT_FAST` outside Pet → dizzy trigger + stats update. Edge hard-bump on menus + settings + reset (reuse the on_scroll_edge pattern B already uses for HUD).
- **D3 — One-shot animations**. Celebrate level-up moment (50 K token accumulation). Deep confetti + strong motor wiggle (`hw_motor_wiggle` + pulse series). Runs once per level-up, driven by `statsPollLevelUp()`.
- **D4 — Clock enrichment**. Small buddy in clock lower area. Mood-based state on clock: Friday afternoon celebrate, weekend hearts, 1-7 am sleep, 10 pm+ dizzy. Upstream buddy did these in its clock render; port the schedule + use buddyRenderTo to paint a compact character alongside the time.

Suggested order: **D2 + D3** first (safe pure-software additions, no motor risk), then **D1** (SimpleFOC is the biggest risk and will want focused debugging), then **D4** (best done after D1 so the clock haptics are polished).

## Sub-project E — content (17 species + CJK font)

- Re-enable 16 dormant ASCII species files in `src/buddies/` (capybara is the only one in the build filter). Each needs the same `#include <M5StickCPlus.h>` removal pattern we used on capybara in Phase 1.
- ASCII pet cycling UI (settings → ascii pet → next species) finally becomes useful once there's more than one.
- CJK font support so Chinese prompts from Claude Desktop render. Either bundle a small GB2312 point-font and write a UTF-8 → glyph lookup, or move to LVGL with a TTF subset (LVGL is a big dependency).
- Prompts from Claude Desktop often include tool names and hints in the user's language. Today they render as `??` or empty boxes in Chinese.

## Sub-project F — WiFi + NTP independent time source

- User called this "a key stability experience". Today after power loss the clock is blank until Claude Desktop syncs again.
- Needs: WiFi SSID/password entry UI (menu item + input method), NVS storage, SNTP client, fallback/reconcile logic when both BLE-time and NTP-time arrive.
- Clock's value increases dramatically once F lands.

## Sub-project G — screen sleep + backlight timeout (newly identified)

- Upstream buddy auto-dimmed then cut LDO2 after 30 s idle (`M5.Axp.SetLDO2(false)`). Phase 1 flagged this as "future follow-up" but never implemented.
- X-Knob path: ledc PWM to 0 + optional deep sleep with EXT0 wake (same mechanism as the menu's Turn Off — just triggered by inactivity).
- Low priority until user runs on battery regularly.

## Open hardware issues

- **Device powers off on USB unplug** — user suspects dead battery or damaged MT3608. Firmware fix for `BATTERY_OFF` inversion shipped as commit `8c696a5` but never verified on device. User will measure battery voltage / MT3608 output when convenient.

## Roadmap stretch (not planned, only mentioned)

- **OTA updates over BLE** — convenience for day-to-day upgrades without USB. Depends on added BLE chunking and NVS partition dance.
- **Surface Dial HID mode** — upstream X-Knob exposed the knob as a USB HID peripheral for Windows Surface Dial gestures / scrolling. Phase 1 dropped it. Would give the device value even when Claude isn't running.
- **Startup log silence** — purely cosmetic; ESP-IDF framework dumps verbose `[I][...]` during boot.

## How to apply / resume

- When user says "continue Phase 2" or "start D / E / F / G", open this file and surface the chosen sub-project. Usually follow the brainstorm → plan → subagent-driven workflow.
- When user asks "what's left?" — summarize the D / E / F / G rows plus the polish backlog and hardware item.
- Before any hardware-touching sub-project (D1 especially), remind the user of the collaboration split (Claude codes + builds, user flashes + tests).
- Don't assume the user has verified everything from the last sub-project — explicitly ask about the "not yet verified" list items at the start of the next hardware session.
