#include "hw_motor.h"
#include <Arduino.h>
#include <SimpleFOC.h>
#include <math.h>

// X-Knob motor pins
static const int MO1 = 17;
static const int MO2 = 16;
static const int MO3 = 15;

// MT6701 pins for hardware SPI (must use HSPI, not default SPI!)
static const int MT6701_SCLK = 2;
static const int MT6701_MISO = 1;
static const int MT6701_SS   = 42;

static const int spiClk = 1000000; // 1MHz

// --- SimpleFOC objects ----------------------------------------------------

BLDCMotor motor = BLDCMotor(7);  // 7 pole pairs
BLDCDriver3PWM driver = BLDCDriver3PWM(MO1, MO2, MO3);

// Use HSPI for MT6701 to avoid conflict with display on default SPI
SPIClass* hspi = NULL;

// MT6701 sensor callbacks
static float readMySensorCallback(void) {
    hspi->beginTransaction(SPISettings(spiClk, MSBFIRST, SPI_MODE0));
    digitalWrite(hspi->pinSS(), LOW);
    uint16_t ag = hspi->transfer16(0);
    digitalWrite(hspi->pinSS(), HIGH);
    hspi->endTransaction();
    ag = ag >> 2;  // 14-bit angle
    float rad = (float)ag * 2.0f * PI / 16384.0f;
    if (rad < 0) rad += 2.0f * PI;
    return rad;
}

static void initMySensorCallback(void) {
    hspi = new SPIClass(HSPI);
    hspi->begin(MT6701_SCLK, MT6701_MISO, -1, MT6701_SS); // SCLK, MISO, MOSI, SS
    pinMode(hspi->pinSS(), OUTPUT);
}

GenericSensor sensor = GenericSensor(readMySensorCallback, initMySensorCallback);

// --- Spring / detent config (from X-Knob original) ------------------------

struct SpringConfig {
    float position_width_rad;
    float detent_strength_unit;
    float endstop_strength_unit;
    float snap_point;
};

static SpringConfig _spring_cfg = {
    .position_width_rad = 8.225806452f * PI / 180.0f,
    .detent_strength_unit = 2.3f,
    .endstop_strength_unit = 1.0f,
    .snap_point = 1.1f
};

static float current_detent_center = 0.0f;
static float angle_to_detent_center = 0.0f;
static int32_t _position = 0;
static bool _foc_enabled = false;

// Idle correction params (from X-Knob original)
static const float IDLE_VELOCITY_EWMA_ALPHA = 0.001f;
static const float IDLE_VELOCITY_RAD_PER_SEC = 0.05f;
static const uint32_t IDLE_CORRECTION_DELAY_MILLIS = 500;
static const float IDLE_CORRECTION_MAX_ANGLE_RAD = 5.0f * PI / 180.0f;
static const float IDLE_CORRECTION_RATE_ALPHA = 0.0005f;

static uint32_t last_idle_start = 0;
static float idle_check_velocity_ewma = 0;

// Dead zone params
static const float DEAD_ZONE_DETENT_PERCENT = 0.2f;
static const float DEAD_ZONE_RAD = 1.0f * PI / 180.0f;

static float CLAMP(const float value, const float low, const float high) {
    return value < low ? low : (value > high ? high : value);
}

// --- Effect state machine (replaces open-loop haptic) ---------------------

enum EffectType {
    EFFECT_NONE,
    EFFECT_CLICK,      // single torque pulse
    EFFECT_KICK,       // directional push
    EFFECT_WIGGLE,     // L-R-L pattern
    EFFECT_PURR,       // continuous alternating
    EFFECT_VIBRATE,    // fast alternating
    EFFECT_PULSE_SERIES
};

struct EffectState {
    EffectType type;
    uint32_t start_ms;
    uint32_t duration_ms;
    float strength;        // 0..1 (normalized to voltage_limit)
    uint8_t direction;     // for kick: 0 or 1
    uint32_t next_pulse_ms;
    bool pulse_phase;      // alternating direction
    uint8_t remaining;
    uint16_t gap_ms;
};

// Written by Core 0 (main loop), read by Core 1 (motor task)
static volatile EffectState _pending_effect = {EFFECT_NONE, 0, 0, 0, 0, 0, false, 0, 0};
static volatile bool _effect_trigger = false;

// Internal copy used by motor task (Core 1 only)
static EffectState _active_effect = {EFFECT_NONE, 0, 0, 0, 0, 0, false, 0, 0};

static float calculate_effect_torque(EffectState& eff, uint32_t now) {
    switch (eff.type) {
        case EFFECT_CLICK: {
            // Single pulse: ramp up then down over 30ms
            uint32_t elapsed = now - eff.start_ms;
            if (elapsed > 30) return 0;
            float envelope = sinf((float)elapsed * PI / 30.0f);  // 0 -> 1 -> 0
            return eff.strength * 5.0f * envelope;  // 5V max (full voltage_limit)
        }
        case EFFECT_KICK: {
            uint32_t elapsed = now - eff.start_ms;
            if (elapsed > 40) return 0;
            float sign = (eff.direction == 0) ? 1.0f : -1.0f;
            return sign * eff.strength * 5.0f;
        }
        case EFFECT_WIGGLE: {
            uint32_t elapsed = now - eff.start_ms;
            // 3 pulses: 0-60ms (+), 80-140ms (-), 160-220ms (+)
            if (elapsed > 220) return 0;
            float sign = 0;
            if (elapsed < 60) sign = 1.0f;
            else if (elapsed < 80) return 0;
            else if (elapsed < 140) sign = -1.0f;
            else if (elapsed < 160) return 0;
            else if (elapsed < 220) sign = 1.0f;
            return sign * eff.strength * 5.0f;
        }
        case EFFECT_PURR: {
            if (now >= eff.start_ms + eff.duration_ms) return 0;
            if (now >= eff.next_pulse_ms) {
                eff.pulse_phase = !eff.pulse_phase;
                eff.next_pulse_ms = now + 150;  // 150ms cadence
            }
            float sign = eff.pulse_phase ? 1.0f : -1.0f;
            return sign * eff.strength * 3.5f;
        }
        case EFFECT_VIBRATE: {
            if (now >= eff.start_ms + eff.duration_ms) return 0;
            if (now >= eff.next_pulse_ms) {
                eff.pulse_phase = !eff.pulse_phase;
                eff.next_pulse_ms = now + 40;  // 40ms = 25Hz
            }
            float sign = eff.pulse_phase ? 1.0f : -1.0f;
            return sign * eff.strength * 4.0f;
        }
        case EFFECT_PULSE_SERIES: {
            if (eff.remaining == 0) return 0;
            if (now >= eff.next_pulse_ms) {
                eff.remaining--;
                eff.next_pulse_ms = now + eff.gap_ms;
                eff.start_ms = now;  // re-use for pulse timing
            }
            uint32_t pulse_elapsed = now - eff.start_ms;
            if (pulse_elapsed < 25) {
                float envelope = sinf((float)pulse_elapsed * PI / 25.0f);
                return eff.strength * 5.0f * envelope;
            }
            return 0;
        }
        default:
            return 0;
    }
}

// --- Task for motor update ------------------------------------------------

TaskHandle_t _motor_task_handle = nullptr;

void TaskMotorUpdate(void *pvParameters) {
    // Initialize sensor and motor (same as original)
    sensor.init();
    motor.linkSensor(&sensor);
    
    driver.voltage_power_supply = 5;
    driver.init();
    motor.linkDriver(&driver);
    
    // FOC modulation mode (CRITICAL!)
    motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
    
    motor.controller = MotionControlType::torque;
    motor.PID_velocity.P = 1;
    motor.PID_velocity.I = 0;
    motor.PID_velocity.D = 0.01f;
    motor.voltage_limit = 5;
    motor.LPF_velocity.Tf = 0.01f;
    motor.velocity_limit = 10;
    
    motor.init();
    motor.initFOC();  // Auto-detect zero electric angle
    
    // X-Knob hardware uses inverted rotation (same as -DXK_INVERT_ROTATION=1 in original)
    current_detent_center = -motor.shaft_angle;
    _foc_enabled = true;
    
    Serial.println("[foc] motor initialized");
    
    while(1) {
        sensor.update();
        motor.loopFOC();
        
        uint32_t now = millis();
        
        // Check for new effect trigger (Core 0 -> Core 1)
        if (_effect_trigger) {
            _effect_trigger = false;
            _active_effect.type = _pending_effect.type;
            _active_effect.start_ms = _pending_effect.start_ms;
            _active_effect.duration_ms = _pending_effect.duration_ms;
            _active_effect.strength = _pending_effect.strength;
            _active_effect.direction = _pending_effect.direction;
            _active_effect.next_pulse_ms = _pending_effect.next_pulse_ms;
            _active_effect.pulse_phase = _pending_effect.pulse_phase;
            _active_effect.remaining = _pending_effect.remaining;
            _active_effect.gap_ms = _pending_effect.gap_ms;
        }
        
        // Calculate spring/detent torque
        float spring_torque = 0;
        
        // Idle velocity check
        idle_check_velocity_ewma = motor.shaft_velocity * IDLE_VELOCITY_EWMA_ALPHA 
                                    + idle_check_velocity_ewma * (1 - IDLE_VELOCITY_EWMA_ALPHA);
        if (fabsf(idle_check_velocity_ewma) > IDLE_VELOCITY_RAD_PER_SEC) {
            last_idle_start = 0;
        } else {
            if (last_idle_start == 0) {
                last_idle_start = now;
            }
        }
        
        // Idle correction: slowly adjust centerpoint when stationary
        if (last_idle_start > 0 && now - last_idle_start > IDLE_CORRECTION_DELAY_MILLIS
                && fabsf(-motor.shaft_angle - current_detent_center) < IDLE_CORRECTION_MAX_ANGLE_RAD) {
            current_detent_center = -motor.shaft_angle * IDLE_CORRECTION_RATE_ALPHA 
                                    + current_detent_center * (1 - IDLE_CORRECTION_RATE_ALPHA);
        }
        
        // Calculate angle to detent center (inverted for X-Knob hardware)
        angle_to_detent_center = -motor.shaft_angle - current_detent_center;
        
        // Handle position snapping
        if (angle_to_detent_center > _spring_cfg.position_width_rad * _spring_cfg.snap_point) {
            current_detent_center += _spring_cfg.position_width_rad;
            angle_to_detent_center -= _spring_cfg.position_width_rad;
            _position--;
        } else if (angle_to_detent_center < -_spring_cfg.position_width_rad * _spring_cfg.snap_point) {
            current_detent_center -= _spring_cfg.position_width_rad;
            angle_to_detent_center += _spring_cfg.position_width_rad;
            _position++;
        }
        
        // Dead zone adjustment (exactly as original)
        float dead_zone_adjustment = CLAMP(
            angle_to_detent_center,
            fmaxf(-_spring_cfg.position_width_rad * DEAD_ZONE_DETENT_PERCENT, -DEAD_ZONE_RAD),
            fminf(_spring_cfg.position_width_rad * DEAD_ZONE_DETENT_PERCENT, DEAD_ZONE_RAD));
        
        // Dynamic PID settings
        motor.PID_velocity.limit = 3;
        motor.PID_velocity.P = _spring_cfg.detent_strength_unit * 4;
        
        // Speed protection (CRITICAL! Prevents runaway)
        if (fabsf(motor.shaft_velocity) > 60) {
            spring_torque = 0;
        } else {
            spring_torque = motor.PID_velocity(-angle_to_detent_center + dead_zone_adjustment);
            spring_torque = -spring_torque;  // Invert for X-Knob hardware
        }
        
        // Overlay effect torque (if any)
        float effect_torque = 0;
        bool effect_active = false;
        if (_active_effect.type != EFFECT_NONE) {
            effect_torque = calculate_effect_torque(_active_effect, now);
            effect_active = (effect_torque != 0 || _active_effect.type == EFFECT_PURR 
                             || _active_effect.type == EFFECT_VIBRATE
                             || _active_effect.type == EFFECT_PULSE_SERIES);
            if (!effect_active) {
                // One-shot effect finished
                _active_effect.type = EFFECT_NONE;
            }
        }
        
        // Apply torque: one-shot effects override spring so they are felt clearly
        if (effect_active && _active_effect.type != EFFECT_PURR && _active_effect.type != EFFECT_VIBRATE) {
            motor.move(effect_torque);  // pure effect, no spring opposition
        } else {
            motor.move(spring_torque + effect_torque);  // spring + continuous effects
        }
        
        vTaskDelay(1);  // 1ms = 1kHz
    }
}

// --- Public API -----------------------------------------------------------

void hw_motor_init() {
    // In FOC mode, initialization happens in hw_motor_init_foc().
    // This stub is kept for API compatibility.
}

void hw_motor_init_foc() {
    xTaskCreatePinnedToCore(
        TaskMotorUpdate,
        "MotorTask",
        4096,
        nullptr,
        2,
        &_motor_task_handle,
        1  // Run on Core 1
    );
}

bool hw_motor_foc_enabled() {
    return _foc_enabled;
}

float hw_motor_foc_angle_deg() {
    return motor.shaft_angle * 180.0f / PI;
}

int32_t hw_motor_position() {
    return _position;
}

void hw_motor_set_detent_config(float width_deg, float strength, float snap) {
    _spring_cfg.position_width_rad = width_deg * PI / 180.0f;
    _spring_cfg.detent_strength_unit = strength;
    _spring_cfg.snap_point = snap;
}

// --- Effect triggers (called from Core 0, safe via volatile) --------------

// Haptic level 0..4 maps to motor strength
static const float HAPTIC_LEVEL_STRENGTH[5] = {0.0f, 0.3f, 0.5f, 0.7f, 1.0f};

static float level_to_strength(uint8_t level) {
    if (level > 4) level = 3;
    return HAPTIC_LEVEL_STRENGTH[level];
}

void hw_motor_click(uint8_t strength) {
    // In FOC mode: trigger a torque pulse
    if (!_foc_enabled) return;
    _pending_effect.type = EFFECT_CLICK;
    _pending_effect.start_ms = millis();
    _pending_effect.duration_ms = 30;
    _pending_effect.strength = strength / 255.0f;
    _pending_effect.direction = 0;
    _pending_effect.next_pulse_ms = 0;
    _pending_effect.pulse_phase = false;
    _pending_effect.remaining = 0;
    _pending_effect.gap_ms = 0;
    _effect_trigger = true;
}

void hw_motor_click_default() {
    extern uint8_t current_haptic_level();
    uint8_t level = current_haptic_level();
    if (level == 0) return;
    hw_motor_click((uint8_t)(level_to_strength(level) * 255));
}

void hw_motor_off() {
    // In FOC mode: no-op (spring keeps running, or could disable)
    // For now, just stop any active effect
    _pending_effect.type = EFFECT_NONE;
    _pending_effect.start_ms = 0;
    _pending_effect.duration_ms = 0;
    _pending_effect.strength = 0;
    _pending_effect.direction = 0;
    _pending_effect.next_pulse_ms = 0;
    _pending_effect.pulse_phase = false;
    _pending_effect.remaining = 0;
    _pending_effect.gap_ms = 0;
    _effect_trigger = true;
}

void hw_motor_purr_start(uint8_t level) {
    if (level == 0 || !_foc_enabled) return;
    _pending_effect.type = EFFECT_PURR;
    _pending_effect.start_ms = millis();
    _pending_effect.duration_ms = 0xFFFFFFFF;
    _pending_effect.strength = level_to_strength(level);
    _pending_effect.direction = 0;
    _pending_effect.next_pulse_ms = millis() + 150;
    _pending_effect.pulse_phase = false;
    _pending_effect.remaining = 0;
    _pending_effect.gap_ms = 150;
    _effect_trigger = true;
}

void hw_motor_purr_stop() {
    if (_active_effect.type == EFFECT_PURR) {
        _active_effect.type = EFFECT_NONE;
    }
}

bool hw_motor_purr_active() {
    return _active_effect.type == EFFECT_PURR;
}

void hw_motor_kick(uint8_t direction, uint8_t level) {
    if (!_foc_enabled) return;
    _pending_effect.type = EFFECT_KICK;
    _pending_effect.start_ms = millis();
    _pending_effect.duration_ms = 40;
    _pending_effect.strength = level_to_strength(level);
    _pending_effect.direction = direction;
    _pending_effect.next_pulse_ms = 0;
    _pending_effect.pulse_phase = false;
    _pending_effect.remaining = 0;
    _pending_effect.gap_ms = 0;
    _effect_trigger = true;
}

void hw_motor_wiggle() {
    if (!_foc_enabled) return;
    _pending_effect.type = EFFECT_WIGGLE;
    _pending_effect.start_ms = millis();
    _pending_effect.duration_ms = 220;
    _pending_effect.strength = HAPTIC_LEVEL_STRENGTH[3];
    _pending_effect.direction = 0;
    _pending_effect.next_pulse_ms = 0;
    _pending_effect.pulse_phase = false;
    _pending_effect.remaining = 0;
    _pending_effect.gap_ms = 0;
    _effect_trigger = true;
}

void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level) {
    if (level == 0 || n == 0 || !_foc_enabled) return;
    _pending_effect.type = EFFECT_PULSE_SERIES;
    _pending_effect.start_ms = millis();
    _pending_effect.duration_ms = (uint32_t)n * gap_ms;
    _pending_effect.strength = level_to_strength(level);
    _pending_effect.direction = 0;
    _pending_effect.next_pulse_ms = millis();
    _pending_effect.pulse_phase = false;
    _pending_effect.remaining = n;
    _pending_effect.gap_ms = gap_ms;
    _effect_trigger = true;
}

void hw_motor_vibrate(uint16_t duration_ms, uint8_t level) {
    if (level == 0 || duration_ms == 0 || !_foc_enabled) return;
    _pending_effect.type = EFFECT_VIBRATE;
    _pending_effect.start_ms = millis();
    _pending_effect.duration_ms = duration_ms;
    _pending_effect.strength = level_to_strength(level);
    _pending_effect.direction = 0;
    _pending_effect.next_pulse_ms = millis() + 40;
    _pending_effect.pulse_phase = false;
    _pending_effect.remaining = 0;
    _pending_effect.gap_ms = 40;
    _effect_trigger = true;
}

void hw_motor_tick(uint32_t now_ms) {
    // In FOC mode, all effects run in the motor task.
    // This function is kept for API compatibility but does nothing.
    (void)now_ms;
}

// --- Legacy spring API (now handled by FOC task, kept for compatibility) ---

void hw_motor_set_spring(float center_deg, float range_deg,
                         float max_strength, float curve_exp) {
    (void)center_deg; (void)range_deg; (void)max_strength; (void)curve_exp;
    // Spring is always active in FOC mode
}

void hw_motor_disable_spring() {
    // Cannot disable spring in FOC mode (it's the core behavior)
}

bool hw_motor_spring_enabled() {
    return _foc_enabled;
}

void hw_motor_set_detents(float position_width_deg, float snap_point,
                          float detent_strength, float endstop_strength) {
    hw_motor_set_detent_config(position_width_deg, detent_strength, snap_point);
    // endstop_strength stored but not used yet
    (void)endstop_strength;
}

void hw_motor_disable_detents() {
    // Set very wide detents effectively disabling them
    _spring_cfg.position_width_rad = 360.0f * PI / 180.0f;
}

float hw_motor_spring_position() {
    return (float)_position;
}

void hw_motor_spring_recenter() {
    current_detent_center = -motor.shaft_angle;
    _position = 0;
}

float hw_motor_get_current_angle() {
    return hw_motor_foc_angle_deg();
}
