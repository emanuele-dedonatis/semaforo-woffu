#pragma once

#include <Arduino.h>

// Sustituto de Serial.printf()/Serial.println() que antepone un timestamp a cada
// linea: hora local si el NTP ya sincrono (ver TimeSync::kNtpSyncedThreshold),
// o segundos desde el arranque si todavia no.
void logPrintf(const char* format, ...) __attribute__((format(printf, 1, 2)));
void logPrintln(const String& message);
void logPrintln(const char* message);

// Ademas de por Serial, las ultimas kLogHistoryLines lineas se guardan en un
// buffer circular en RAM, numeradas con un contador creciente (seq, empieza en
// 1), para que RemoteLink pueda reenviarlas al servidor: tanto las que se
// generaron antes de conectar (arranque, portal...) como las nuevas, sin
// depender de un callback que se ejecute en medio de cada logPrintf().
// Solo se llama desde loopTask (igual que el resto del log), sin locking.
constexpr size_t kLogHistoryLines = 32;
constexpr size_t kLogLineMaxLen = 200;

// Numero de secuencia de la ultima linea registrada (0 si todavia ninguna).
uint32_t logLastSeq();
// Llama a fn(seq, line) para cada linea guardada con seq > afterSeq, en orden.
// Las que ya salieron del buffer circular se pierden sin aviso.
void logForEachSince(uint32_t afterSeq, void (*fn)(uint32_t seq, const char* line, void* ctx), void* ctx);
