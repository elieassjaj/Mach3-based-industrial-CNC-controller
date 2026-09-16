#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

extern int g_fail;
extern int g_checks;
extern int g_case_failed;
extern const char *g_case;

#define TCASE(name) do { g_case = (name); printf("  %-56s", (name)); fflush(stdout); } while (0)
#define TDONE()     do { printf("%s\n", g_case_failed ? "FAIL" : "ok"); g_case_failed = 0; } while (0)

#define CHECK(cond) do {                                                      \
    g_checks++;                                                               \
    if (!(cond)) {                                                            \
        g_fail++; g_case_failed = 1;                                          \
        printf("\n    ASSERT %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                         \
} while (0)

#define CHECK_EQI(a, b) do {                                                  \
    long long _a = (long long)(a), _b = (long long)(b);                       \
    g_checks++;                                                               \
    if (_a != _b) {                                                           \
        g_fail++; g_case_failed = 1;                                          \
        printf("\n    ASSERT %s:%d: %s == %s  (%lld != %lld)\n",              \
               __FILE__, __LINE__, #a, #b, _a, _b);                           \
    }                                                                         \
} while (0)

#define CHECK_GE(a, b) do {                                                   \
    long long _a = (long long)(a), _b = (long long)(b);                       \
    g_checks++;                                                               \
    if (!(_a >= _b)) {                                                        \
        g_fail++; g_case_failed = 1;                                          \
        printf("\n    ASSERT %s:%d: %s >= %s  (%lld < %lld)\n",               \
               __FILE__, __LINE__, #a, #b, _a, _b);                           \
    }                                                                         \
} while (0)

#endif /* TEST_UTIL_H */
