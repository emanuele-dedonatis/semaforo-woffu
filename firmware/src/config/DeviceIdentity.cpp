#include "DeviceIdentity.h"

#include <esp_mac.h>

namespace {
void readMac(uint8_t mac[6]) {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}
}

namespace DeviceIdentity {

String name() {
    uint8_t mac[6] = {0};
    readMac(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "Semaforo-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return String(buf);
}

String password() {
    uint8_t mac[6] = {0};
    readMac(mac);
    uint32_t value = 0;
    for (uint8_t b : mac) {
        value = (value * 31) + b;
    }
    char buf[9];
    snprintf(buf, sizeof(buf), "%08u", static_cast<unsigned>(value % 100000000UL));
    return String(buf);
}

}
