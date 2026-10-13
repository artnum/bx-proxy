#include "fresh.h"

BxAge bx_age(int present, uint64_t last_updated, int deleted, time_t now,
             int stale_seconds) {
  uint64_t window;

  if (!present || deleted) {
    return BX_MISSING;
  }
  if (stale_seconds <= 0) {
    stale_seconds = 7200;
  }
  window = (uint64_t)stale_seconds;
  if (last_updated == 0 || (uint64_t)now < last_updated) {
    return last_updated == 0 ? BX_STALE : BX_FRESH;
  }
  if ((uint64_t)now - last_updated >= window) {
    return BX_STALE;
  }
  return BX_FRESH;
}
