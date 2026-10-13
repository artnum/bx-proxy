#ifndef BX_PROXY_HTTP_H__
#define BX_PROXY_HTTP_H__

#include "serve.h"
#include <signal.h>

typedef struct BxHttp BxHttp;

BxHttp *bx_http_new(BxServe *serve, const char *host, int port);
int bx_http_run(BxHttp *http, volatile sig_atomic_t *stop);
void bx_http_free(BxHttp *http);

#endif
