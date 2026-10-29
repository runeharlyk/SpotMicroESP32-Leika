#pragma once
// Host stand-in: one heap, which every capability asks of.
#include <cstddef>
#include <cstdlib>
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_SPIRAM (1 << 10)
inline void *heap_caps_malloc_prefer(size_t size, size_t, ...) { return malloc(size); }
