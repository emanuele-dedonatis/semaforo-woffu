#include <Arduino.h>
#include "app/AppStateMachine.h"
#include "Version.h"

AppStateMachine app;

void setup() {
    Serial.begin(115200);
    Serial.printf("Semaforo Woffu - firmware %s\n", FIRMWARE_VERSION);
    app.begin();
}

void loop() {
    app.loop();
}
