#include "actuators/servo_control.h"
#include "config.h"
#include "utils/logger.h"
#include <ESP32Servo.h>
#include <Arduino.h>

static Servo _servo;

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
    // Standard frequency for SG90 servos is 50Hz
    _servo.setPeriodHertz(50);

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
    // Bound the degrees between our defined minimum and maximum limits
    if (degrees < SERVO_POS_IN) degrees = SERVO_POS_IN;
    if (degrees > SERVO_POS_OUT) degrees = SERVO_POS_OUT;
    
    // If the PWM signal was previously detached to save power, re-attach it
    if (!_servo.attached()) {
        _servo.attach(PIN_SERVO, 500, 2400);
    }
    
    _servo.write(degrees);
}

void servoControlDetach() {
    // Stop sending the PWM signal.
    // This stops the servo from jittering and drawing idle current.
    if (_servo.attached()) {
        _servo.detach();
        // Drive the pin LOW so the signal line never floats (a floating
        // line can make the servo twitch).
        pinMode(PIN_SERVO, OUTPUT);
        digitalWrite(PIN_SERVO, LOW);
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