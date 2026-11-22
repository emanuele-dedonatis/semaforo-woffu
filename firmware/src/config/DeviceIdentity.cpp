#include "DeviceIdentity.h"

#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>

namespace DeviceIdentity {

String name() {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[24];
    snprintf(buf, sizeof(buf), "Semaforo-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(buf);
}

String token() {
    static String cached;
    if (!cached.isEmpty()) {
        return cached;
    }
    Preferences prefs;
    prefs.begin("dev", false);
    cached = prefs.getString("token", "");
    if (cached.length() != 32) {
        // esp_random() es un RNG hardware: con la radio WiFi/BT encendida da
        // aleatoriedad real; sin ella, el bootloader ya lo ha sembrado con
        // ruido de RF/ADC (suficiente para un secreto que se genera una vez).
        char buf[33];
        for (int i = 0; i < 4; i++) {
            snprintf(buf + i * 8, 9, "%08x", static_cast<unsigned>(esp_random()));
        }
        cached = String(buf);
        prefs.putString("token", cached);
    }
    prefs.end();
    return cached;
}

}
