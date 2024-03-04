#pragma once

#include <globals.h>

float getHeading();

float getTemp();

float getAngleX();

float getAngleY();

float getAngleZ();

void IRAM_ATTR MovementHandlingLoopEntry(void *);