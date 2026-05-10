#include <Arduino.h>
#include <ESP32Encoder.h>

#define PIN_LED 2
#define PIN_SW  14
#define PIN_DT  33
#define PIN_CLK 32

ESP32Encoder encoder;
long lastPosition = 0;

void flash(int repetitions, int duration) {
  for (int i = 0; i < repetitions; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(duration);
    digitalWrite(PIN_LED, LOW);
    if (i < repetitions - 1) delay(duration);
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_SW, INPUT_PULLUP);

  // Encoder Configuration
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachHalfQuad(PIN_DT, PIN_CLK);
  encoder.setCount(0);
}

void loop() {
  long currentPosition = encoder.getCount();

  // 1. Rotation Detection
  if (currentPosition > lastPosition) {
    Serial.println("Right ->");
    flash(1, 50); // 1 quick flash
    lastPosition = currentPosition;
  } 
  else if (currentPosition < lastPosition) {
    Serial.println("<- Left");
    flash(2, 50); // 2 quick flashes
    lastPosition = currentPosition;
  }

  // 2. Button Detection (SW)
  if (digitalRead(PIN_SW) == LOW) {
    Serial.println("Button Pressed");
    flash(1, 400); // 1 long flash
    while(digitalRead(PIN_SW) == LOW); // Wait for release
  }

  delay(10); 
}