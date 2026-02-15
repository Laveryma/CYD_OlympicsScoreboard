#pragma once

#include <Arduino.h>

namespace Anthem {

void begin();
bool playNow();
bool playNowForMs(uint32_t maxDurationMs);

}  // namespace Anthem
