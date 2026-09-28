#pragma once

// Host stand-in for ESP-DSP's dspm_mult.h. The tested headers include it through utils/math_utils.h
// but do not call the matrix multiply; like the real header, it brings in esp_err_t.
#include <esp_err.h>
