#ifndef BX_PROXY_FRESH_H__
#define BX_PROXY_FRESH_H__

#include <stdint.h>
#include <time.h>

typedef enum BxAge { BX_FRESH = 0, BX_STALE = 1, BX_MISSING = 2 } BxAge;

/* present is 0 when no row. deleted is non-zero when _deleted is set.
 * stale_seconds <= 0 means 7200. A timestamp in the future is fresh. */
BxAge bx_age(int present, uint64_t last_updated, int deleted, time_t now,
             int stale_seconds);

#endif
