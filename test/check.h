#pragma once
#include <cstdio>

// Minimales Test-Geruest ohne externe Abhaengigkeiten.
static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    g_checks++;                                                         \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      g_failures++;                                                     \
    }                                                                   \
  } while (0)

static int finish(const char *name) {
  std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
