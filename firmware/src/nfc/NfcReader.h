#pragma once

#include <Adafruit_PN532.h>
#include <Arduino.h>

// Envuelve el lector PN532 (SPI) para exponer un polling no bloqueante,
// "edge-triggered": poll() solo devuelve true la primera vez que detecta una
// tarjeta tras no haber ninguna, para que un tap se procese una sola vez
// mientras la tarjeta siga apoyada en el lector (ver AppStateMachine, donde
// se usa tanto para el aprendizaje como para el fichaje normal).
class NfcReader {
public:
    // SPI hardware (bus VSPI por defecto de la placa), pinCs es el unico pin
    // dedicado en exclusiva al lector. Devuelve false si no se detecta el
    // PN532 (p.ej. cableado incorrecto o modulo en otro modo HSU/I2C); el
    // fallo no es fatal, el resto del firmware sigue funcionando sin NFC.
    bool begin(uint8_t pinCs);

    // true solo en la transicion ausente -> presente. uidHexOut queda con el
    // UID en hexadecimal mayusculas sin separadores (p.ej. "04A1B2C3").
    bool poll(String& uidHexOut);

private:
    // Adafruit_PN532 fija el pin SS en el constructor (constructor de SPI
    // hardware: Adafruit_PN532(ss)), así que se instancia dentro de begin()
    // en vez de como miembro directo, para mantener begin(pinCs) parametrizado
    // igual que LedController::begin(pines...).
    Adafruit_PN532* pn532_ = nullptr;
    bool cardPresent_ = false;
    uint8_t missCount_ = 0;
};
