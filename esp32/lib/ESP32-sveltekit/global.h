#ifndef GLOBAL_H
#define GLOBAL_H

#define NAME_OF(x) #x

#define ACTUAL_NAME_OF(x) ((#x) + 1)

#define FIRST_OCCURRENCE(str, ch) strchr((str), (ch))

#define END_OF_STRING(str) ((str) + strlen(str) - 1)

#define COPY_SUBSTRING(dest, start, end) \
    do {                                 \
        int len = (end) - (start);       \
        strncpy((dest), (start), len);   \
        (dest)[len] = '\0';              \
    } while (0)

#define CHECK_AND_ADVANCE(ptr, ch) \
    do {                           \
        if (*(ptr) == (ch)) {      \
            (ptr)++;               \
        }                          \
    } while (0)

#define CONCAT(a, b) a##b

#define UNIQUE_VAR(base) CONCAT(base, __LINE__)

inline void* preferPSRAMAlloc(size_t s) { return psramInit() ? ps_malloc(s) : malloc(s); }

#endif