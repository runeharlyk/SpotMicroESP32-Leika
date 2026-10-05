#pragma once

// Forced into every translation unit of a host test that counts nanopb's allocations (-include counting_alloc.h).
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int countingAllocLive;

static inline void *countingRealloc(void *ptr, size_t size) {
    if (!ptr) countingAllocLive++;
    return realloc(ptr, size);
}

static inline void countingFree(void *ptr) {
    if (ptr) countingAllocLive--;
    free(ptr);
}

#ifdef __cplusplus
}
#endif

#define pb_realloc(ptr, size) countingRealloc(ptr, size)
#define pb_free(ptr) countingFree(ptr)
