#include "actuators/servo_control.h"
#include "config.h"
#include <ESP32Servo.h>
#include <Arduino.h>

static Servo _servo;

void servoControlInit() {
    // Standard frequency for SG90 servos is 50Hz
    _servo.setPeriodHertz(50);
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
    }
}