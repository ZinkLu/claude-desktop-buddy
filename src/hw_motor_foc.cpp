#include "hw_motor.h"
#include <Arduino.h>
#include <SimpleFOC.h>
#include <math.h>

// ---- Hardware pins (X-Knob) ---------------------------------------------

static const int MO1 = 17, MO2 = 16, MO3 = 15;
static const int MT6701_SCLK = 2, MT6701_MISO = 1, MT6701_SS = 42;

// ---- SimpleFOC objects --------------------------------------------------

static BLDCMotor motor = BLDCMotor(7);
static BLDCDriver3PWM driver = BLDCDriver3PWM(MO1, MO2, MO3);
static SPIClass* hspi = NULL;

static float readMySensorCallback(void) {
    hspi->beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(hspi->pinSS(), LOW);
    uint16_t ag = hspi->transfer16(0);
    digitalWrite(hspi->pinSS(), HIGH);
    hspi->endTransaction();
    ag = ag >> 2;
    float rad = (float)ag * 2.0f * PI / 16384.0f;
    if (rad < 0) rad += 2.0f * PI;
    return rad;
}

static void initMySensorCallback(void) {
    hspi = new SPIClass(HSPI);
    hspi->begin(MT6701_SCLK, MT6701_MISO, -1, MT6701_SS);
    pinMode(hspi->pinSS(), OUTPUT);
}

static GenericSensor sensor = GenericSensor(readMySensorCallback, initMySensorCallback);

// ---- Spring / detent config (from X-Knob original) ----------------------

struct SpringConfig {
    float position_width_rad;
    float detent_strength_unit;
    float endstop_strength_unit;
    float snap_point;
};

static SpringConfig _spring_cfg = {
    .position_width_rad   = 8.225806452f * PI / 180.0f,
    .detent_strength_unit = 2.3f,
    .endstop_strength_unit = 1.0f,
    .snap_point           = 1.1f
};

static float   current_detent_center = 0;
static float   angle_to_detent_center = 0;
static int32_t _position = 0;
static bool    _foc_enabled = false;

// Haptic level (0..4), set from Core 0, read by Core 1
static volatile uint8_t _haptic_level = 3;
static const float HAPTIC_DETENT_SCALE[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

// Idle correction (from X-Knob original)
static const float    IDLE_VELOCITY_EWMA_ALPHA      = 0.001f;
static const float    IDLE_VELOCITY_RAD_PER_SEC      = 0.05f;
static const uint32_t IDLE_CORRECTION_DELAY_MILLIS   = 500;
static const float    IDLE_CORRECTION_MAX_ANGLE_RAD  = 5.0f * PI / 180.0f;
static const float    IDLE_CORRECTION_RATE_ALPHA     = 0.0005f;
static uint32_t last_idle_start = 0;
static float    idle_check_velocity_ewma = 0;

// Dead zone (from X-Knob original)
static const float DEAD_ZONE_DETENT_PERCENT = 0.2f;
static const float DEAD_ZONE_RAD            = 1.0f * PI / 180.0f;

static float CLAMP(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

// ---- Effect system ------------------------------------------------------

enum EffectType {
    EFFECT_NONE,
    EFFECT_CLICK,
    EFFECT_WIGGLE,
    EFFECT_VIBRATE,
    EFFECT_PULSE_SERIES
};

struct EffectRequest {
    EffectType type;
    uint32_t   start_ms;
    uint32_t   duration_ms;
    float      strength;
    uint32_t   next_pulse_ms;
    uint8_t    remaining;
    uint16_t   gap_ms;
    bool       phase;
};

// Lock-free SPSC ring buffer (Core 0 writes, Core 1 reads)
static const uint8_t EFFQ_SIZE = 8;
static volatile bool          _effq_has_data = false;
static volatile uint8_t       _effq_write_idx = 0;
static volatile EffectRequest _effq_buf[EFFQ_SIZE];
static uint8_t                _effq_read_idx = 0;

// Active effect (Core 1 only)
static EffectRequest _active = {EFFECT_NONE};

// Motor activity cooldown — input module reads this to suppress
// motor-induced shaft movement being interpreted as user rotation.
static volatile uint32_t _motor_busy_until_ms = 0;

// Purr runs on a separate channel (not through the queue) because it's
// continuous and stop/start semantics don't fit the one-shot queue model.
static volatile bool    _purr_requested = false;
static volatile uint8_t _purr_level = 0;
static bool     _purr_active_core1 = false;
static bool     _purr_phase = false;
static uint32_t _purr_next_ms = 0;

// ---- Haptic level mapping -----------------------------------------------

static const float HAPTIC_STRENGTH[5] = {0.0f, 0.3f, 0.5f, 0.7f, 1.0f};

static float level_to_strength(uint8_t level) {
    if (level > 4) level = 3;
    return HAPTIC_STRENGTH[level];
}

// ---- Effect torque calculation ------------------------------------------

static float calculate_effect_torque(EffectRequest& e, uint32_t now) {
    switch (e.type) {
    case EFFECT_CLICK: {
        uint32_t elapsed = now - e.start_ms;
        if (elapsed > 30) return 0;
        // Full sine cycle: balanced push-pull (net zero drift)
        float envelope = sinf((float)elapsed * 2.0f * PI / 30.0f);
        return e.strength * 5.0f * envelope;
    }
    case EFFECT_WIGGLE: {
        uint32_t elapsed = now - e.start_ms;
        if (elapsed > 220) return 0;
        float sign = 0;
        if      (elapsed <  60) sign =  1.0f;
        else if (elapsed <  80) return 0;
        else if (elapsed < 140) sign = -1.0f;
        else if (elapsed < 160) return 0;
        else                    sign =  1.0f;
        return sign * e.strength * 5.0f;
    }
    case EFFECT_VIBRATE: {
        if (now >= e.start_ms + e.duration_ms) return 0;
        if (now >= e.next_pulse_ms) {
            e.next_pulse_ms = now + 40;
            e.phase = !e.phase;
        }
        return (e.phase ? 1.0f : -1.0f) * e.strength * 4.0f;
    }
    case EFFECT_PULSE_SERIES: {
        uint32_t pulse_elapsed = now - e.start_ms;
        if (now >= e.next_pulse_ms && e.remaining > 0) {
            e.remaining--;
            e.start_ms = now;
            e.next_pulse_ms = now + e.gap_ms;
            pulse_elapsed = 0;
        }
        if (pulse_elapsed < 25) {
            float envelope = sinf((float)pulse_elapsed * PI / 25.0f);
            return e.strength * 5.0f * envelope;
        }
        if (e.remaining == 0) return 0;
        return 0;
    }
    default:
        return 0;
    }
}

// ---- Motor task (Core 1, 1 kHz) -----------------------------------------

static TaskHandle_t _motor_task_handle = nullptr;

static void TaskMotorUpdate(void*) {
    // Initialization sequence matches original X-Knob motor.cpp
    sensor.init();
    motor.linkSensor(&sensor);

    driver.voltage_power_supply = 5;
    driver.init();
    motor.linkDriver(&driver);

    motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
    motor.controller     = MotionControlType::torque;
    motor.PID_velocity.P = 1;
    motor.PID_velocity.I = 0;
    motor.PID_velocity.D = 0.01f;
    motor.voltage_limit  = 5;
    motor.LPF_velocity.Tf = 0.01f;
    motor.velocity_limit = 10;

    motor.init();
    motor.initFOC();

    current_detent_center = -motor.shaft_angle;
    _foc_enabled = true;
    Serial.println("[foc] motor initialized");

    while (1) {
        sensor.update();
        motor.loopFOC();
        uint32_t now = millis();

        // ---- Drain effect queue (Core 0 → Core 1) ----------------------
        if (_effq_has_data) {
            uint8_t wr = _effq_write_idx;
            while (_effq_read_idx != wr) {
                volatile EffectRequest& src = _effq_buf[_effq_read_idx];
                _active.type         = src.type;
                _active.start_ms     = src.start_ms;
                _active.duration_ms  = src.duration_ms;
                _active.strength     = src.strength;
                _active.next_pulse_ms = src.next_pulse_ms;
                _active.remaining    = src.remaining;
                _active.gap_ms       = src.gap_ms;
                _active.phase        = src.phase;
                _effq_read_idx = (_effq_read_idx + 1) % EFFQ_SIZE;
                Serial.printf("[foc] effect type=%d str=%.2f rem=%u\n",
                              _active.type, _active.strength, _active.remaining);
            }
            _effq_has_data = false;
        }

        // ---- Purr management --------------------------------------------
        if (_purr_requested) {
            if (!_purr_active_core1) {
                _purr_active_core1 = true;
                _purr_phase = false;
                _purr_next_ms = now + 150;
                _motor_busy_until_ms = now + 200;
            }
        } else if (_purr_active_core1) {
            _purr_active_core1 = false;
            _motor_busy_until_ms = now + 200;
        }

        // ---- One-shot effect torque -------------------------------------
        float effect_torque = 0;
        if (_active.type != EFFECT_NONE) {
            effect_torque = calculate_effect_torque(_active, now);
            bool still_active = (effect_torque != 0);
            if (!still_active && _active.type == EFFECT_PULSE_SERIES && _active.remaining > 0)
                still_active = true;
            if (!still_active)
                _active.type = EFFECT_NONE;
        }

        // ---- Purr torque ------------------------------------------------
        float purr_torque = 0;
        if (_purr_active_core1) {
            if (now >= _purr_next_ms) {
                _purr_phase = !_purr_phase;
                _purr_next_ms = now + 150;
                _motor_busy_until_ms = now + 200;
            }
            purr_torque = (_purr_phase ? 1.0f : -1.0f) * level_to_strength(_purr_level) * 5.0f;
        }

        // ---- Spring / detent torque (from X-Knob original) ---------------
        float spring_torque = 0;

        idle_check_velocity_ewma = motor.shaft_velocity * IDLE_VELOCITY_EWMA_ALPHA
                                 + idle_check_velocity_ewma * (1 - IDLE_VELOCITY_EWMA_ALPHA);
        if (fabsf(idle_check_velocity_ewma) > IDLE_VELOCITY_RAD_PER_SEC) {
            last_idle_start = 0;
        } else if (last_idle_start == 0) {
            last_idle_start = now;
        }

        if (last_idle_start > 0
            && now - last_idle_start > IDLE_CORRECTION_DELAY_MILLIS
            && fabsf(-motor.shaft_angle - current_detent_center) < IDLE_CORRECTION_MAX_ANGLE_RAD) {
            current_detent_center = -motor.shaft_angle * IDLE_CORRECTION_RATE_ALPHA
                                  + current_detent_center * (1 - IDLE_CORRECTION_RATE_ALPHA);
        }

        angle_to_detent_center = -motor.shaft_angle - current_detent_center;

        if (angle_to_detent_center > _spring_cfg.position_width_rad * _spring_cfg.snap_point) {
            current_detent_center += _spring_cfg.position_width_rad;
            angle_to_detent_center -= _spring_cfg.position_width_rad;
            _position--;
        } else if (angle_to_detent_center < -_spring_cfg.position_width_rad * _spring_cfg.snap_point) {
            current_detent_center -= _spring_cfg.position_width_rad;
            angle_to_detent_center += _spring_cfg.position_width_rad;
            _position++;
        }

        float dead_zone = CLAMP(
            angle_to_detent_center,
            fmaxf(-_spring_cfg.position_width_rad * DEAD_ZONE_DETENT_PERCENT, -DEAD_ZONE_RAD),
            fminf( _spring_cfg.position_width_rad * DEAD_ZONE_DETENT_PERCENT,  DEAD_ZONE_RAD));

        float haptic_scale = HAPTIC_DETENT_SCALE[_haptic_level < 5 ? _haptic_level : 4];
        motor.PID_velocity.limit = 3;
        motor.PID_velocity.P = _spring_cfg.detent_strength_unit * 4 * haptic_scale;

        if (haptic_scale == 0 || fabsf(motor.shaft_velocity) > 60) {
            spring_torque = 0;
        } else {
            spring_torque = -motor.PID_velocity(-angle_to_detent_center + dead_zone);
        }

        // ---- Final torque: spring + effects overlay ---------------------
        float total = spring_torque + effect_torque + purr_torque;
        if (effect_torque != 0 || purr_torque != 0)
            _motor_busy_until_ms = now + 80;
        motor.move(total);

        vTaskDelay(1);
    }
}

// ---- Public API (called from Core 0) ------------------------------------

void hw_motor_init_foc() {
    xTaskCreatePinnedToCore(TaskMotorUpdate, "MotorTask", 4096,
                            nullptr, 2, &_motor_task_handle, 1);
}

bool hw_motor_foc_enabled()    { return _foc_enabled; }
float hw_motor_foc_angle_deg() { return motor.shaft_angle * 180.0f / PI; }
uint32_t hw_motor_busy_until() { return _motor_busy_until_ms; }
int32_t hw_motor_position()    { return _position; }

bool hw_motor_effect_active() {
    return _active.type != EFFECT_NONE || _purr_active_core1;
}

// ---- Effect queue (Core 0 → Core 1, lock-free SPSC) --------------------

static void effect_enqueue(const EffectRequest& req) {
    if (!_foc_enabled) return;
    uint8_t next = (_effq_write_idx + 1) % EFFQ_SIZE;
    if (next == _effq_read_idx) return;

    volatile EffectRequest& dst = _effq_buf[_effq_write_idx];
    dst.type         = req.type;
    dst.start_ms     = req.start_ms;
    dst.duration_ms  = req.duration_ms;
    dst.strength     = req.strength;
    dst.next_pulse_ms = req.next_pulse_ms;
    dst.remaining    = req.remaining;
    dst.gap_ms       = req.gap_ms;
    dst.phase        = req.phase;
    _effq_write_idx  = next;
    _effq_has_data   = true;
}

void hw_motor_click(uint8_t strength) {
    EffectRequest req = {};
    req.type     = EFFECT_CLICK;
    req.start_ms = millis();
    req.strength = strength / 255.0f;
    effect_enqueue(req);
}

void hw_motor_click_default() {
    extern uint8_t current_haptic_level();
    uint8_t level = current_haptic_level();
    if (level == 0) return;
    hw_motor_click((uint8_t)(level_to_strength(level) * 255));
}

void hw_motor_wiggle() {
    if (!_foc_enabled) return;
    EffectRequest req = {};
    req.type     = EFFECT_WIGGLE;
    req.start_ms = millis();
    req.strength = HAPTIC_STRENGTH[3];
    effect_enqueue(req);
}

void hw_motor_pulse_series(uint8_t n, uint16_t gap_ms, uint8_t level) {
    if (level == 0 || n == 0 || !_foc_enabled) return;
    EffectRequest req = {};
    req.type         = EFFECT_PULSE_SERIES;
    req.start_ms     = millis();
    req.duration_ms  = (uint32_t)n * gap_ms;
    req.strength     = level_to_strength(level);
    req.next_pulse_ms = millis();
    req.remaining    = n;
    req.gap_ms       = gap_ms;
    effect_enqueue(req);
}

void hw_motor_vibrate(uint16_t duration_ms, uint8_t level) {
    if (level == 0 || duration_ms == 0 || !_foc_enabled) return;
    EffectRequest req = {};
    req.type         = EFFECT_VIBRATE;
    req.start_ms     = millis();
    req.duration_ms  = duration_ms;
    req.strength     = level_to_strength(level);
    req.next_pulse_ms = millis() + 40;
    effect_enqueue(req);
}

void hw_motor_purr_start(uint8_t level) {
    if (level == 0 || !_foc_enabled) return;
    _purr_level = level;
    _purr_requested = true;
}

void hw_motor_purr_stop()    { _purr_requested = false; }
bool hw_motor_purr_active()  { return _purr_active_core1; }

void hw_motor_set_haptic(uint8_t level) {
    _haptic_level = (level > 4) ? 4 : level;
    Serial.printf("[foc] haptic=%u scale=%.0f%%\n", _haptic_level,
                  HAPTIC_DETENT_SCALE[_haptic_level] * 100);
}

void hw_motor_off() {
    _effq_write_idx = _effq_read_idx;
    _effq_has_data = false;
    _active.type = EFFECT_NONE;
    _purr_requested = false;
}
