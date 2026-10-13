#ifndef BX_PROXY_SERVE_H__
#define BX_PROXY_SERVE_H__

#include "bxill.h"
#include <mysql/mysql.h>
#include <stddef.h>

typedef struct BxServe BxServe;

BxServe *bx_serve_new(bXill *app, int stale_seconds);
void bx_serve_free(BxServe *serve);
int bx_serve_connect(BxServe *serve);
void bx_serve_close(BxServe *serve);
void bx_serve_reconnect(BxServe *serve);
/* Reconnect when the last request hit a dead MariaDB connection. */
void bx_serve_recover(BxServe *serve);

/* Fills *status, *body (caller frees), and proxy ("hit", "refresh", "stale",
 * or empty). Returns 0 when *body is set. */
int bx_serve_request(BxServe *serve, const char *path, const char *query,
                     int is_get, int *status, char **body, char *proxy,
                     size_t proxy_len);

#endif
