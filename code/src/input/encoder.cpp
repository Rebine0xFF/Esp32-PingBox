#include "input/encoder.h"
#include "config.h"
#include <ESP32Encoder.h>

static ESP32Encoder enc;


static int _duration_minutes = 0;   // défault at startup

static const int MIN_MINUTES = 0;
static const int MAX_MINUTES = 90;

void encoderInit() {

    ESP32Encoder::useInternalWeakPullResistors = puType::up;

    // SingleEdge = 1 clic physique = 1 incrément → 1 minute par cran
    enc.attachHalfQuad(PIN_ENC_CLK, PIN_ENC_DT);

    // On initialise le compteur à la valeur par défaut
    enc.setCount(_duration_minutes);
}


int encoderGetMinutes() {

    int raw = (int)enc.getCount();

    if (raw < MIN_MINUTES) {
        enc.setCount(MIN_MINUTES);
        raw = MIN_MINUTES;
    }
    if (raw > MAX_MINUTES) {
        enc.setCount(MAX_MINUTES);
        raw = MAX_MINUTES;
    }

    _duration_minutes = raw;
    return _duration_minutes;
}