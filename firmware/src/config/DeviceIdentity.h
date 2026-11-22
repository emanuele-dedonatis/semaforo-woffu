#pragma once

#include <Arduino.h>

// Identidad del dispositivo derivada de la MAC: el nombre (SSID del AP del
// portal, "Semaforo-XXXXXX") y la password de 8 digitos de ese mismo AP.
// La misma pareja sirve como usuario/password para autenticarse contra el
// servidor remoto (RemoteLink) y para entrar en la web app que este sirve
// (ver Arquitectura.md, `## Control remoto`).
namespace DeviceIdentity {
String name();
String password();
}
