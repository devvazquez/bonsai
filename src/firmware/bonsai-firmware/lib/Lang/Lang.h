#pragma once
#include <Arduino.h>

// The language, implemented in main.cpp (which owns the config and saves it).
// In lib/ and not include/ because lib/ libraries do not see include/.
//
//     bonsai::lang();          // "ca" — always two lowercase letters
//     bonsai::setLang("es");   // stores it in the config on the SD card
namespace bonsai {

String lang();
void   setLang(const String& nou);

}  // namespace bonsai
