#include "CertBundle.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

void applyCertBundle(WiFiClientSecure& client) {
    client.setCACertBundle(rootca_crt_bundle_start);
}
