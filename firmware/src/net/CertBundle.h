#pragma once

#include <WiFiClientSecure.h>

// Adjunta el bundle de CAs de Mozilla embebido (data/cert/x509_crt_bundle.bin)
// a un WiFiClientSecure, para validar TLS sin pinnear certificados.
void applyCertBundle(WiFiClientSecure& client);

// El mismo bundle en crudo, para librerias que gestionan su propio
// WiFiClientSecure internamente (RemoteLink / WebSocketsClient).
const uint8_t* certBundle();
