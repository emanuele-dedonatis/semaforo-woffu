#include "WifiManager.h"
#include <WiFi.h>

void WifiManager::begin(const String& ssid, const String& password) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
}

bool WifiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ipAddress() const {
    return WiFi.localIP().toString();
}
