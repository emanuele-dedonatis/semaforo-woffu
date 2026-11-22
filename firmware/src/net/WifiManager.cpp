#include "WifiManager.h"
#include <WiFi.h>

void WifiManager::begin(const String& ssid, const String& password) {
    // No fuerza WiFi.mode(WIFI_STA): WiFi.begin() habilita STA vía enableSTA(),
    // que combina el bit con el modo AP si el portal de provisioning ya está activo.
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid.c_str(), password.c_str());
}

bool WifiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WifiManager::ipAddress() const {
    return WiFi.localIP().toString();
}
