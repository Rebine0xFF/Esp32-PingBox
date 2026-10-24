#include "actuators/servo_control.h"
#include "config.h"
#include "utils/logger.h"
#include <Arduino.h>


// ------------------------------------------------------------
//  Direct LEDC PWM (no servo library). ESP32Servo 3.x does not match the
//  Arduino-ESP32 2.0.17 core used by this project and caused erratic servo
//  behavior. LEDC works on both 2.x and 3.x cores.
// ------------------------------------------------------------
static constexpr int      SERVO_LEDC_CHANNEL = 4;    // used on core 2.x only
static constexpr uint32_t SERVO_PWM_FREQ_HZ  = 50;
static constexpr uint8_t  SERVO_PWM_BITS     = 16;

static bool _signalOn = false;   // true while a PWM signal is being sent to the servo

static void _pwmWriteDuty(uint32_t duty) {
    #if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcWrite(PIN_SERVO, duty);
    #else
        ledcWrite(SERVO_LEDC_CHANNEL, duty);
    #endif
}

// ------------------------------------------------------------
//  Flag motion tuning (module-local on purpose: only this file uses them).
//  The SG90 has no position feedback, so safety relies on bounded drive
//  time: slow stepped moves, a short settle hold, then the PWM is cut.
// ------------------------------------------------------------
static constexpr int      FLAG_MOVE_STEP_DEG       = 2;      // degrees per step for raise/lower
static constexpr uint32_t FLAG_MOVE_STEP_MS        = 15;     // ~130 deg/s
static constexpr int      FLAG_SHAKE_STEP_DEG      = 4;      // faster steps while shaking
static constexpr uint32_t FLAG_SHAKE_STEP_MS       = 12;
static constexpr int      FLAG_SHAKE_AMPLITUDE_DEG = 12;     // swing below SERVO_POS_OUT
static constexpr int      FLAG_SHAKE_CYCLES        = 3;
static constexpr uint32_t FLAG_SETTLE_MS           = 300;    // keep the signal after the last step
static constexpr uint32_t FLAG_MAX_DRIVE_MS        = 3000;   // hard cut-off per move

enum class MotionPhase { IDLE, MOVING, SETTLING };

static MotionPhase _phase         = MotionPhase::IDLE;
static bool        _flagUp        = false;           // logical state: true once a raise was requested
static int         _posDeg        = SERVO_POS_IN;    // estimated position (open loop)
static int         _targetDeg     = SERVO_POS_IN;
static int         _stepDeg       = FLAG_MOVE_STEP_DEG;
static uint32_t    _stepMs        = FLAG_MOVE_STEP_MS;
static uint32_t    _lastStepMs    = 0;
static uint32_t    _settleStartMs = 0;
static uint32_t    _driveStartMs  = 0;
static int         _shakeLegsLeft = 0;


void servoControlInit() {

    if (ENABLE_SERVO) {
        #if ESP_ARDUINO_VERSION_MAJOR >= 3
            ledcAttach(PIN_SERVO, SERVO_PWM_FREQ_HZ, SERVO_PWM_BITS);
        #else
            ledcSetup(SERVO_LEDC_CHANNEL, SERVO_PWM_FREQ_HZ, SERVO_PWM_BITS);
            ledcAttachPin(PIN_SERVO, SERVO_LEDC_CHANNEL);
        #endif
            _pwmWriteDuty(0);   // no signal until the first move
    }

    // Homing: the flag may have been left anywhere (e.g. power cut while
    // raised). Write the rest position once, hold it briefly, then cut
    // the signal (done by servoControlUpdate()).
    _flagUp        = false;
    _posDeg        = SERVO_POS_IN;
    _targetDeg     = SERVO_POS_IN;
    _shakeLegsLeft = 0;
    servoControlSetPosition(SERVO_POS_IN);
    _driveStartMs  = millis();
    _settleStartMs = _driveStartMs;
    _phase         = MotionPhase::SETTLING;
}

void servoControlSetPosition(int degrees) {
    // Servo output disabled at build time: never touch the pin.
    if (!ENABLE_SERVO) return;

    // Bound the degrees between our defined minimum and maximum limits
    if (degrees < SERVO_POS_IN) degrees = SERVO_POS_IN;
    if (degrees > SERVO_POS_OUT) degrees = SERVO_POS_OUT;

    // Map degrees (0-180) to a pulse width, then to a 16-bit LEDC duty
    // for the 20 ms servo period.
    uint32_t pulseUs = SERVO_PULSE_MIN_US
                     + (uint32_t)(SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) * degrees / 180;
    uint32_t duty = (uint32_t)((uint64_t)pulseUs * ((1UL << SERVO_PWM_BITS) - 1)
                               / (1000000UL / SERVO_PWM_FREQ_HZ));
    _pwmWriteDuty(duty);
    _signalOn = true;
}

void servoControlDetach() {
    if (!ENABLE_SERVO) return;

    // Duty 0 keeps the pin at a constant LOW: no pulses, so the servo stops
    // driving its motor and draws almost no current. The LEDC channel stays
    // attached (no attach/detach cycles).
    if (_signalOn) {
        _pwmWriteDuty(0);
        _signalOn = false;
    }
}


// ------------------------------------------------------------
//  Flag motion (non-blocking)
// ------------------------------------------------------------

static void _beginMove(int target, int stepDeg, uint32_t stepMs) {
    _targetDeg    = target;
    _stepDeg      = stepDeg;
    _stepMs       = stepMs;
    _driveStartMs = millis();
    _lastStepMs   = _driveStartMs - stepMs;   // first step happens on the next update
    _phase        = MotionPhase::MOVING;
}

static void _startShakeLeg() {
    // Even count -> swing down, odd count -> back to the raised position.
    int target = (_shakeLegsLeft % 2 == 0) ? (SERVO_POS_OUT - FLAG_SHAKE_AMPLITUDE_DEG)
                                           : SERVO_POS_OUT;
    _beginMove(target, FLAG_SHAKE_STEP_DEG, FLAG_SHAKE_STEP_MS);
}

void servoFlagLower() {
    if (!_flagUp) return;   // already down (or on its way down)

    _flagUp        = false;
    _shakeLegsLeft = 0;
    LOG_INFO("SERVO", "Flag: lowering");
    _beginMove(SERVO_POS_IN, FLAG_MOVE_STEP_DEG, FLAG_MOVE_STEP_MS);
}

void servoFlagAcknowledged() {
    if (!_flagUp) {
        _flagUp        = true;
        _shakeLegsLeft = 0;
        LOG_INFO("SERVO", "Flag: raising");
        _beginMove(SERVO_POS_OUT, FLAG_MOVE_STEP_DEG, FLAG_MOVE_STEP_MS);
        return;
    }

    if (_shakeLegsLeft > 0) return;   // already shaking
    if (_phase == MotionPhase::MOVING && _targetDeg == SERVO_POS_OUT) return;   // still rising

    LOG_INFO("SERVO", "Flag: already up, shaking");
    _shakeLegsLeft = 2 * FLAG_SHAKE_CYCLES;
    _startShakeLeg();
}

void servoControlUpdate() {
    if (_phase == MotionPhase::IDLE) return;

    uint32_t now = millis();

    // Safety net: never keep driving longer than FLAG_MAX_DRIVE_MS per move.
    if (now - _driveStartMs > FLAG_MAX_DRIVE_MS) {
        LOG_WARN("SERVO", "Drive time exceeded, cutting the signal");
        servoControlDetach();
        _phase         = MotionPhase::IDLE;
        _shakeLegsLeft = 0;
        return;
    }

    if (_phase == MotionPhase::SETTLING) {
        if (now - _settleStartMs >= FLAG_SETTLE_MS) {
            servoControlDetach();
            _phase = MotionPhase::IDLE;
        }
        return;
    }

    // MOVING: at most one step per call, so a stalled loop never
    // produces a large sudden jump.
    if (now - _lastStepMs < _stepMs) return;
    _lastStepMs = now;

    int diff = _targetDeg - _posDeg;
    int step = (diff > 0) ? diff : -diff;
    if (step > _stepDeg) step = _stepDeg;
    _posDeg += (diff > 0) ? step : -step;
    servoControlSetPosition(_posDeg);

    if (_posDeg != _targetDeg) return;

    // Target reached: continue the shake sequence if one is running.
    if (_shakeLegsLeft > 0) {
        _shakeLegsLeft--;
        if (_shakeLegsLeft > 0) {
            _startShakeLeg();
            return;
        }
    }

    _phase         = MotionPhase::SETTLING;
    _settleStartMs = now;
}

bool servoControlIsBusy() {
    return _phase != MotionPhase::IDLE;
}