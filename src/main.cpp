#include <Arduino.h>
#include "app/AppStateMachine.h"

AppStateMachine app;

void setup() {
    Serial.begin(115200);
    app.begin();
}

void loop() {
    app.loop();
}
