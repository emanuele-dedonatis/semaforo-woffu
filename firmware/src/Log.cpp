#include "Log.h"

#include <stdarg.h>
#include <time.h>

#include "net/TimeSync.h"

namespace {
void printTimestamp() {
    time_t now = time(nullptr);
    if (now > kNtpSyncedThreshold) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        Serial.printf("[%02d:%02d:%02d] ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.printf("[+%lus] ", millis() / 1000);
    }
}
}  // namespace

void logPrintf(const char* format, ...) {
    printTimestamp();
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    Serial.print(buf);
}

void logPrintln(const String& message) {
    printTimestamp();
    Serial.println(message);
}

void logPrintln(const char* message) {
    printTimestamp();
    Serial.println(message);
}
