#pragma once
#include <Arduino.h>
#include "mining_core.h"

extern StratumJob job;

bool stratumConnect(uint8_t poolNum=1);
void stratumTick();
