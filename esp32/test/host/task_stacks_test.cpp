// Host test of the task stack report, built and run by test_host_programs.py.
#include <cstdio>
#include <cstring>
#include <utils/task_stacks.h>

static int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// The tightest stacks lead, so a task near overflow is never cut off the end of a full report.
static void theTightestStacksLeadAndTheRestAreCut() {
    TaskStack tasks[] = {{"Service task", 2400}, {"Serial link", 5800}, {"Control task", 900}, {"IDLE0", 600}};
    const size_t shown = tightestStacks(tasks, 4, 3);
    CHECK(shown == 3);
    CHECK(std::strcmp(tasks[0].name, "IDLE0") == 0);
    CHECK(std::strcmp(tasks[1].name, "Control task") == 0);
    CHECK(std::strcmp(tasks[2].name, "Service task") == 0);
}

// Ties order by name, so two reports of the same tasks list them alike.
static void tiesKeepAStableOrder() {
    TaskStack tasks[] = {{"b", 100}, {"a", 100}};
    CHECK(tightestStacks(tasks, 2, 8) == 2);
    CHECK(std::strcmp(tasks[0].name, "a") == 0);
}

int main() {
    theTightestStacksLeadAndTheRestAreCut();
    tiesKeepAStableOrder();
    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
