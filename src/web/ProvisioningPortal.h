#pragma once

#include <Arduino.h>
#include "config/Config.h"

class WebServer;
class DNSServer;

class ProvisioningPortal {
public:
    void begin(const DeviceConfig& current);
    void loop();
    void stop();
    bool hasClient() const;

    bool takeConfigToSave(DeviceConfig& out);
    bool takeOtaRequested();
    bool takeFactoryResetRequested();

private:
    void handleRoot();
    void handleSave();
    void handleOta();
    void handleFactoryReset();
    void handleNotFound();

    WebServer* server_ = nullptr;
    DNSServer* dns_ = nullptr;
    DeviceConfig current_;

    bool pendingSave_ = false;
    DeviceConfig pendingConfig_;
    bool pendingOta_ = false;
    bool pendingFactoryReset_ = false;
};
