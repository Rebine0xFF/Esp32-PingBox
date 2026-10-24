#include <Arduino.h>
#include <ESP32Servo.h>

const int PIN_SERVO = 18;   // ESP32-S3 : GPIO19 = USB D-, passe sur 18/13/14/25/26
const int POS_IN    = 35;   // pas 20
const int POS_OUT   = 145;  // pas 160
const int TRAVEL_MS = 900;  // SG90 chargé : 500–800 ms pour ~80°

Servo servo;

void moveTo(int angle) {
  if (!servo.attached()) {
    servo.setPeriodHertz(50);
    servo.attach(PIN_SERVO, 1000, 2000); // pas 500/2400
    servo.attach(PIN_SERVO, 1000, 2000); // 2e attach : bug LEDC core 3.x / S3
  }
  servo.write(angle);   // APRÈS attach, sinon c’est ignoré
  delay(TRAVEL_MS);     // le PWM doit rester là pendant tout le trajet
  servo.detach();
  pinMode(PIN_SERVO, OUTPUT);
  digitalWrite(PIN_SERVO, LOW); // tue le buzz de broche flottante
}

void setup() {
  Serial.begin(115200);
  moveTo(POS_IN);
}

void loop() {
  delay(3000);
  moveTo(POS_OUT);
  delay(3000);
  moveTo(POS_IN);
}