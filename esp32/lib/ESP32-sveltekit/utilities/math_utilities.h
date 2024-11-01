#ifndef MATHUTILS_H
#define MATHUTILS_H

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

#define INT_TO_STRING(state, output)      \
    do {                                  \
        itoa((int)(state), (output), 10); \
    } while (0)

#define PI_F 3.1415927f

#define DEG2RAD_F 0.0174532f

#define RAD2DEG_F 57.2957795f

#define RAD_TO_DEG_F(rad) ((rad) * RAD2DEG_F)

#define DEG_TO_RAD_F(deg) ((deg) * DEG2RAD_F)

#define COS_DEG_F(deg) (cosf(DEG_TO_RAD_F(deg)))

#define SIN_DEG_F(deg) (sinf(DEG_TO_RAD_F(deg)))

#define IS_EQUAL(a, b, epsilon) (std::fabs((a) - (b)) < (epsilon))

#define IS_ALMOST_EQUAL(a, b) IS_EQUAL((a), (b), 0.001f)

inline float lerp(float start, float end, float t) { return (1 - t) * start + t * end; }

inline bool isEqual(float a, float b, float epsilon) { return std::fabs(a - b) < epsilon; }

inline float round2(float value) { return (int)(value * 100 + 0.5) / 100.0; }

inline bool arrayEqual(const float arr1[4][4], const float arr2[4][4], float epsilon = 1e-3) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (std::fabs(arr1[i][j] - arr2[i][j]) > epsilon) {
                return false;
            }
        }
    }
    return true;
}

#endif