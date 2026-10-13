#ifndef BX_PROXY_READ_JSON_H__
#define BX_PROXY_READ_JSON_H__

#include "bx_database.h"

/* One object. Columns whose names start with '_' are omitted.
 * A 16-byte column named uuid is a hex UUID. Caller frees the result. */
char *bx_row_to_json(const BXDatabaseQuery *query, const BXDatabaseRow *row);

/* A JSON array of the result rows. "[]" when there are none. */
char *bx_rows_to_json(const BXDatabaseQuery *query);

#endif
