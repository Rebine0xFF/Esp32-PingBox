#include <Arduino.h>

// Library-free servo test: drives the PWM signal straight from the ESP32 LEDC
// peripheral. Works on both Arduino-ESP32 2.x and 3.x cores.
const int PIN_SERVO     = 18;
const int LEDC_CHANNEL  = 4;       // only used on core 2.x
const int PWM_FREQ_HZ   = 50;
const int PWM_BITS      = 16;
const int PULSE_MIN_US  = 800;     // pulse width for 0 degrees
const int PULSE_MAX_US  = 2100;    // pulse width for 180 degrees

static void pwmWriteDuty(uint32_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(PIN_SERVO, duty);
#else
    ledcWrite(LEDC_CHANNEL, duty);
#endif
}

static void servoWriteAngle(int deg) {
    uint32_t us = PULSE_MIN_US + (uint32_t)(PULSE_MAX_US - PULSE_MIN_US) * deg / 180;
    uint32_t duty = (uint32_t)((uint64_t)us * ((1UL << PWM_BITS) - 1) / (1000000UL / PWM_FREQ_HZ));
    pwmWriteDuty(duty);
}

// Constant LOW on the pin = no servo signal (the channel stays attached).
static void servoSignalOff() {
    pwmWriteDuty(0);
}

static void sweep(int from, int to) {
    int dir = (to > from) ? 1 : -1;
    for (int pos = from; (dir > 0) ? (pos < to) : (pos > to); pos += dir * 2) {
        servoWriteAngle(pos);
        delay(15);
    }
    servoWriteAngle(to);
    delay(300);
    servoSignalOff();
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.printf("Arduino-ESP32 core %d.%d.%d\n",
                  ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_SERVO, PWM_FREQ_HZ, PWM_BITS);
#else
    ledcSetup(LEDC_CHANNEL, PWM_FREQ_HZ, PWM_BITS);
    ledcAttachPin(PIN_SERVO, LEDC_CHANNEL);
#endif

    servoWriteAngle(20);     // rest position
    delay(1000);
    servoSignalOff();
}

void loop() {
    static int cycle = 0;
    Serial.printf("cycle %d: up\n", ++cycle);
    sweep(20, 160);
    delay(3000);
    Serial.println("down");
    sweep(160, 20);
    delay(3000);
}