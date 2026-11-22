#pragma once

#include <Arduino.h>

// Sustituto de Serial.printf()/Serial.println() que antepone un timestamp a cada
// linea: hora local si el NTP ya sincrono (ver TimeSync::kNtpSyncedThreshold),
// o segundos desde el arranque si todavia no.
void logPrintf(const char* format, ...) __attribute__((format(printf, 1, 2)));
void logPrintln(const String& message);
void logPrintln(const char* message);
