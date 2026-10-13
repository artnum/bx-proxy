#ifndef BX_SYNC_MORE_H__
#define BX_SYNC_MORE_H__

#include "bxill.h"
#include <mysql/mysql.h>

BXillError bx_lookups_walk_more(bXill *app, MYSQL *conn);
BXillError bx_records_walk_more(bXill *app, MYSQL *conn);
BXillError bx_kb_sales_walk_items(bXill *app, MYSQL *conn);
BXillError bx_contact_extra_sync(bXill *app, MYSQL *conn, uint64_t contact_id);
BXillError bx_invoice_extra_sync(bXill *app, MYSQL *conn, uint64_t invoice_id);
BXillError bx_project_extra_sync(bXill *app, MYSQL *conn, uint64_t project_id);

/* Fetch one row. key NULL is only valid for company_profile. */
BXillError bx_sync_more_one(bXill *app, MYSQL *conn, const char *table,
                            const char *key);

#endif /* BX_SYNC_MORE_H__ */
