#ifndef BX_SYNC_ONE_H__
#define BX_SYNC_ONE_H__

#include "bxill.h"
#include <jansson.h>
#include <mysql/mysql.h>
#include <stdint.h>

/* GET path_fmt. key NULL means the path has no '$'. On success *out owns
 * the request and request->decoded is the JSON body. Caller frees *out. */
BXillError bx_http_get_json(bXill *app, const char *path_fmt, const char *key,
                            BXNetRequest **out);

/* First object of a bare object, a bare array, or a {data: object|array}. */
json_t *bx_json_unwrap_object(json_t *root);

BXillError bx_sync_one(bXill *app, MYSQL *conn, const char *table, uint64_t id);
/* key NULL refreshes company_profile. Other tables need the id text. */
BXillError bx_sync_one_key(bXill *app, MYSQL *conn, const char *table,
                           const char *key);

#endif /* BX_SYNC_ONE_H__ */
