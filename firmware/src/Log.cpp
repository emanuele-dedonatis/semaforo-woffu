#include "Log.h"

#include <stdarg.h>
#include <time.h>

#include "net/TimeSync.h"

namespace {
char history[kLogHistoryLines][kLogLineMaxLen];
uint32_t lastSeq = 0;

size_t formatTimestamp(char* out, size_t size) {
    time_t now = time(nullptr);
    if (now > kNtpSyncedThreshold) {
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        return snprintf(out, size, "[%02d:%02d:%02d] ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    return snprintf(out, size, "[+%lus] ", millis() / 1000);
}

// Imprime por Serial y guarda en el historial. `text` puede acabar o no en
// '\n' (logPrintf() lo trae ya incluido en el formato, logPrintln() no).
void emit(const char* text) {
    char line[kLogLineMaxLen];
    size_t len = formatTimestamp(line, sizeof(line));
    if (len >= sizeof(line)) {
        len = sizeof(line) - 1;
    }
    strlcpy(line + len, text, sizeof(line) - len);
    size_t total = strlen(line);
    while (total > 0 && (line[total - 1] == '\n' || line[total - 1] == '\r')) {
        line[--total] = '\0';
    }
    Serial.println(line);

    lastSeq++;
    strlcpy(history[lastSeq % kLogHistoryLines], line, kLogLineMaxLen);
}
}  // namespace

void logPrintf(const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    emit(buf);
}

void logPrintln(const String& message) {
    emit(message.c_str());
}

void logPrintln(const char* message) {
    emit(message);
}

uint32_t logLastSeq() {
    return lastSeq;
}

void logForEachSince(uint32_t afterSeq, void (*fn)(uint32_t seq, const char* line, void* ctx), void* ctx) {
    uint32_t oldest = lastSeq >= kLogHistoryLines ? lastSeq - kLogHistoryLines + 1 : 1;
    uint32_t from = afterSeq + 1 > oldest ? afterSeq + 1 : oldest;
    for (uint32_t seq = from; seq <= lastSeq; seq++) {
        fn(seq, history[seq % kLogHistoryLines], ctx);
    }
}
