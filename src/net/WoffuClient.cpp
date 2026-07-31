#include "WoffuClient.h"

void WoffuClient::begin(const String& username, const String& password) {
    username_ = username;
    password_ = password;
}

WoffuStatus WoffuClient::fetchStatus() {
    return WoffuStatus::UNKNOWN;
}

bool WoffuClient::login() {
    return false;
}
