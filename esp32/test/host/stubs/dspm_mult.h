#pragma once
// Host stand-in: math_utils.h includes esp-dsp for its MAT_MULT macro, which the motion code never
// expands; kinematics.h only needs the error type that esp-dsp brings in.
typedef int esp_err_t;
#define ESP_OK 0
