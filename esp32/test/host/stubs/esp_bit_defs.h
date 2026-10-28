#pragma once
// Host stand-in for ESP-IDF's bit macros.
#include <cstdint>
#define BIT64(nr) (1ULL << (nr))
