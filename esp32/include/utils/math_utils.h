#pragma once

#include <dspm_mult.h>
#include <cmath>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

#define COPY_2D_ARRAY_4x4(dest, src) \
    do {                             \
        (dest)[0][0] = (src)[0][0];  \
        (dest)[0][1] = (src)[0][1];  \
        (dest)[0][2] = (src)[0][2];  \
        (dest)[0][3] = (src)[0][3];  \
        (dest)[1][0] = (src)[1][0];  \
        (dest)[1][1] = (src)[1][1];  \
        (dest)[1][2] = (src)[1][2];  \
        (dest)[1][3] = (src)[1][3];  \
        (dest)[2][0] = (src)[2][0];  \
        (dest)[2][1] = (src)[2][1];  \
        (dest)[2][2] = (src)[2][2];  \
        (dest)[2][3] = (src)[2][3];  \
        (dest)[3][0] = (src)[3][0];  \
        (dest)[3][1] = (src)[3][1];  \
        (dest)[3][2] = (src)[3][2];  \
        (dest)[3][3] = (src)[3][3];  \
    } while (0)

#define MAT_MULT(A, B, result, rows, cols, result_cols) \
    dspm_mult_f32_ae32((float *)(A), (float *)(B), (float *)(result), (rows), (cols), (result_cols))

#define DEG2RAD_F 0.0174532f

#define RAD2DEG_F 57.2957795f

#define RAD_TO_DEG_F(rad) ((rad) * RAD2DEG_F)

#define DEG_TO_RAD_F(deg) ((deg) * DEG2RAD_F)

using std::lerp;

inline float clamp(float value, float min_val, float max_val) {
    return value < min_val ? min_val : (value > max_val ? max_val : value);
}

inline bool isEqual(float a, float b, float epsilon) { return std::fabs(a - b) < epsilon; }

inline float round2(float value) { return (int)(value * 100 + 0.5) / 100.0; }

static constexpr float combinatorial_constexpr(const int n, int k) {
    if (k < 0 || k > n) return 0.0f;
    if (k == 0 || k == n) return 1.0f;
    k = (k < (n - k)) ? k : (n - k);
    float result = 1.0f;
    for (int i = 0; i < k; ++i) {
        result *= (n - i);
        result /= (i + 1);
    }
    return result;
}