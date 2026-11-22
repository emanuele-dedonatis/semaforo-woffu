#include "NfcReader.h"

#include "Log.h"

namespace {
// Timeout corto por lectura: readPassiveTargetID() es no bloqueante de forma
// acotada (hace polling interno de ~10ms), asumible una vez por tick de
// loop() sin introducir un retraso perceptible.
constexpr uint16_t kPollTimeoutMs = 50;
// Lecturas fallidas consecutivas antes de dar la tarjeta por retirada: evita
// que un fallo puntual de lectura RF (tarjeta todavia apoyada) se interprete
// como "retirada y vuelta a acercar", que dispararia un doble evento.
constexpr uint8_t kMissesToConsiderRemoved = 3;

String uidToHex(const uint8_t* uid, uint8_t length) {
    String hex;
    hex.reserve(length * 2);
    for (uint8_t i = 0; i < length; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", uid[i]);
        hex += buf;
    }
    return hex;
}
}  // namespace

bool NfcReader::begin(uint8_t pinCs) {
    pn532_ = new Adafruit_PN532(pinCs);
    pn532_->begin();

    uint32_t firmwareVersion = pn532_->getFirmwareVersion();
    if (!firmwareVersion) {
        logPrintln("NFC: lector PN532 no detectado - revisa el cableado SPI y el modo del modulo.");
        delete pn532_;
        pn532_ = nullptr;
        return false;
    }

    pn532_->SAMConfig();
    logPrintf("NFC: lector PN532 detectado (firmware %lu.%lu).\n", (firmwareVersion >> 16) & 0xFF,
              (firmwareVersion >> 8) & 0xFF);
    return true;
}

bool NfcReader::poll(String& uidHexOut) {
    if (pn532_ == nullptr) {
        return false;
    }

    uint8_t uid[7];
    uint8_t uidLength = 0;
    bool detected = pn532_->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, kPollTimeoutMs);

    if (!detected) {
        if (cardPresent_) {
            missCount_++;
            if (missCount_ >= kMissesToConsiderRemoved) {
                cardPresent_ = false;
                missCount_ = 0;
            }
        }
        return false;
    }

    missCount_ = 0;
    if (cardPresent_) {
        // Misma tarjeta todavia apoyada: no es un nuevo tap.
        return false;
    }

    cardPresent_ = true;
    uidHexOut = uidToHex(uid, uidLength);
    return true;
}
