#include <Arduino.h>
#include "app/AppStateMachine.h"
#include "Log.h"
#include "Version.h"

AppStateMachine app;

void setup() {
    Serial.begin(115200);
    logPrintf("Semaforo Woffu - firmware %s\n", FIRMWARE_VERSION);
    app.begin();
}

void loop() {
    app.loop();
}
