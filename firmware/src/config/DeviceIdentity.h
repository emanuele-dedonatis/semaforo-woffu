#pragma once

#include <Arduino.h>

// Identidad del dispositivo frente al servidor de control remoto.
namespace DeviceIdentity {

// "Semaforo-XXXXXX" (ultimos 3 bytes de la MAC): SSID del AP del portal y
// usuario de la web app del servidor.
String name();

// Secreto aleatorio de 32 caracteres hex con el que el dispositivo se
// autentica ante el servidor (HTTP Basic name:token). Se genera la primera vez
// que se pide y se guarda en NVS (namespace "dev", aparte de "cfg" para que un
// reset de fabrica no lo cambie: el servidor lo registra la primera vez que el
// dispositivo se conecta y despues solo acepta ese). Nunca se muestra.
String token();

}
