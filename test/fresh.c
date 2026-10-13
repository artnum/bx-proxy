#include "fresh.h"

#include <stdio.h>

static int fails;

static void check(int cond, const char *file, int line, const char *what) {
  if (!cond) {
    fprintf(stderr, "FAIL %s:%d %s\n", file, line, what);
    fails++;
  }
}

#define CHECK(cond) check((cond), __FILE__, __LINE__, #cond)

int main(void) {
  const time_t now = 1000000;

  CHECK(bx_age(0, now, 0, now, 7200) == BX_MISSING);
  CHECK(bx_age(1, now, 1, now, 7200) == BX_MISSING);
  CHECK(bx_age(1, 0, 0, now, 7200) == BX_STALE);
  CHECK(bx_age(1, now - 7200, 0, now, 7200) == BX_STALE);
  CHECK(bx_age(1, now - 7199, 0, now, 7200) == BX_FRESH);
  CHECK(bx_age(1, now + 10, 0, now, 7200) == BX_FRESH);

  if (fails) {
    fprintf(stderr, "%d freshness checks failed\n", fails);
    return 1;
  }
  printf("fresh ok\n");
  return 0;
}
