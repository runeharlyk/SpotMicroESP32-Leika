#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

/** One task's least free stack since it started, in bytes, as FreeRTOS's high-water mark reports it. */
struct TaskStack {
    const char *name;
    uint32_t freeMin;
};

/**
 * The tasks with the least stack to spare first, at most `capacity` of them: the ones closest to an overflow, and the
 * ones whose stack could be trimmed, read off one list. Sorts `tasks` in place; returns how many fit.
 */
inline size_t tightestStacks(TaskStack *tasks, size_t count, size_t capacity) {
    std::sort(tasks, tasks + count, [](const TaskStack &a, const TaskStack &b) {
        return a.freeMin != b.freeMin ? a.freeMin < b.freeMin : std::strcmp(a.name, b.name) < 0;
    });
    return std::min(count, capacity);
}
