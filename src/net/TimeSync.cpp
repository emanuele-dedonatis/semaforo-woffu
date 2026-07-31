#include "TimeSync.h"
#include <time.h>

void TimeSync::begin(const String& posixTimezone) {
    configTzTime(posixTimezone.c_str(), "pool.ntp.org", "time.nist.gov");
}

bool TimeSync::isSynced() const {
    struct tm timeinfo;
    return getLocalTime(&timeinfo, 0);
}
