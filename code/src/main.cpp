#include <Arduino.h>
#include "config.h"
#include "actuators/servo_control.h"

// -----------------------------------------------------------------
// INDEPENDENT SERVO TEST
// This test sweeps the servo between IN and OUT positions,
// then detaches the PWM signal to ensure it doesn't vibrate.
// -----------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    Serial.println("--- Starting Servo Sweep Test ---");
    
    servoControlInit();
}

void loop() {
    Serial.println("Moving to OUT position...");
    servoControlSetPosition(SERVO_POS_OUT);
    // Wait for the mechanical movement to complete
    delay(500); 
    servoControlDetach();
    
    // Pause for 2 seconds
    delay(2000);
    
    Serial.println("Moving to IN position...");
    servoControlSetPosition(SERVO_POS_IN);
    // Wait for the mechanical movement to complete
    delay(500);
    servoControlDetach();
    
    // Pause for 2 seconds
    delay(2000);
}