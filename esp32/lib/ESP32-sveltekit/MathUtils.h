#ifndef MATHUTILS_H
#define MATHUTILS_H

#include <cmath>

inline float lerp(float start, float end, float t) { return (1 - t) * start + t * end; }

inline bool isEqual(float a, float b, float epsilon) { return std::fabs(a - b) < epsilon; }

inline float round2(float value) { return (int)(value * 100 + 0.5) / 100.0; }

#endif