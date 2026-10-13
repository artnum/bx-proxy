#include "include/bx_sync_one.h"

#include "include/bx_database.h"
#include "include/bx_ids_cache.h"
#include "include/bx_named_list.h"
#include "include/bx_net.h"
#include "include/bx_object_value.h"
#include "include/bx_sync_more.h"
#include "include/bx_utils.h"
#include "include/bxobjects/account.h"
#include "include/bxobjects/contact.h"
#include "include/bxobjects/contact_group.h"
#include "include/bxobjects/contact_sector.h"
#include "include/bxobjects/currency.h"
#include "include/bxobjects/file.h"
#include "include/bxobjects/invoice.h"
#include "include/bxobjects/language.h"
#include "include/bxobjects/project.h"
#include "include/bxobjects/taxes.h"
#include "include/bxobjects/user.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

json_t *bx_json_unwrap_object(json_t *root) {
  json_t *data = NULL;

  if (json_is_array(root)) {
    if (json_array_size(root) == 0) {
      return NULL;
    }
    return json_array_get(root, 0);
  }
  if (!json_is_object(root)) {
    return NULL;
  }
  data = json_object_get(root, "data");
  if (json_is_object(data)) {
    return data;
  }
  if (json_is_array(data)) {
    if (json_array_size(data) == 0) {
      return NULL;
    }
    return json_array_get(data, 0);
  }
  return root;
}

BXillError bx_http_get_json(bXill *app, const char *path_fmt, const char *key,
                            BXNetRequest **out) {
  BXNetRequest *req = NULL;
  json_t *json = NULL;
  json_error_t jerr;
  BXString text;

  if (out != NULL) {
    *out = NULL;
  }
  if (app == NULL || app->queue == NULL || path_fmt == NULL || out == NULL) {
    return ErrorGeneric;
  }
  if (strchr(path_fmt, '$') != NULL) {
    if (key == NULL || key[0] == '\0') {
      return ErrorGeneric;
    }
    memset(&text, 0, sizeof(text));
    text.type = BX_OBJECT_TYPE_STRING;
    text.isset = true;
    text.value = (char *)key;
    text.value_len = strlen(key);
    req = bx_do_request_raw(app->queue, NULL, (char *)path_fmt,
                            (BXGeneric *)&text);
  } else {
    req = bx_do_request_raw(app->queue, NULL, (char *)path_fmt);
  }
  if (req == NULL || req->response == NULL) {
    bx_net_request_free(req);
    return ErrorNet;
  }
  if (req->response->http_code == 404) {
    bx_net_request_free(req);
    return ErrorNotFound;
  }
  if (req->response->http_code != 200 || req->response->data == NULL ||
      req->response->data_len == 0) {
    bx_net_request_free(req);
    return ErrorNet;
  }
  json = json_loadb(req->response->data, req->response->data_len, 0, &jerr);
  if (json == NULL) {
    bx_net_request_free(req);
    return ErrorJSON;
  }
  req->decoded = json;
  *out = req;
  return NoError;
}

static BXUInteger key_uint(const char *key) {
  BXUInteger id;
  memset(&id, 0, sizeof(id));
  id.type = BX_OBJECT_TYPE_UINTEGER;
  id.isset = true;
  if (key != NULL && key[0] != '\0') {
    id.value = strtoull(key, NULL, 10);
  }
  return id;
}

static int key_missing(const char *key) { return key == NULL || key[0] == '\0'; }

static int table_name_ok(const char *name) {
  const char *p;
  if (name == NULL || name[0] == '\0') {
    return 0;
  }
  for (p = name; *p != '\0'; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
      return 0;
    }
  }
  return 1;
}

/* Walks preload this cache. A null cache looks like "row missing" and the
 * contact, invoice, and project writers then INSERT. Seed the stored
 * checksum so an existing row is updated instead. */
static BXillError seed_cache(MYSQL *conn, const char *table, uint64_t id,
                             Cache **out) {
  char sql[96];
  BXDatabaseQuery *query;
  Cache *cache;
  *out = NULL;
  cache = cache_create();
  if (cache == NULL) {
    return ErrorGeneric;
  }
  snprintf(sql, sizeof(sql), "SELECT `_checksum` FROM `%s` WHERE `id` = :id",
           table);
  query = bx_database_new_query(conn, sql);
  if (query == NULL) {
    cache_destroy(cache);
    return ErrorSQLReconnect;
  }
  if (!bx_database_add_param_uint64(query, ":id", &id) ||
      !bx_database_execute(query) || !bx_database_results(query)) {
    BXillError e = query->need_reconnect ? ErrorSQLReconnect : ErrorGeneric;
    bx_database_free_query(query);
    cache_destroy(cache);
    return e;
  }
  if (query->results != NULL && query->results->column_count > 0) {
    cache_set_item(cache, id, (uint64_t)query->results->columns[0].i_value);
  }
  bx_database_free_query(query);
  *out = cache;
  return NoError;
}

/* Checksum-equal writers return without rewriting. The proxy still has to
 * move _last_updated or the next GET is stale again. */
static BXillError touch_updated(MYSQL *conn, const char *table,
                                const char *key) {
  char sql[160];
  BXDatabaseQuery *query;
  uint64_t now = (uint64_t)time(NULL);
  uint64_t id = 0;
  char *end = NULL;
  int numeric = 0;

  if (!table_name_ok(table)) {
    return ErrorGeneric;
  }
  if (key_missing(key)) {
    snprintf(sql, sizeof(sql), "UPDATE `%s` SET `_last_updated` = :ts", table);
  } else {
    snprintf(sql, sizeof(sql),
             "UPDATE `%s` SET `_last_updated` = :ts WHERE `id` = :id", table);
  }
  query = bx_database_new_query(conn, sql);
  if (query == NULL) {
    return ErrorSQLReconnect;
  }
  if (!bx_database_add_param_uint64(query, ":ts", &now)) {
    bx_database_free_query(query);
    return ErrorGeneric;
  }
  if (!key_missing(key)) {
    numeric = 1;
    for (const char *p = key; *p != '\0'; p++) {
      if (*p < '0' || *p > '9') {
        numeric = 0;
        break;
      }
    }
    if (numeric) {
      errno = 0;
      id = strtoull(key, &end, 10);
      if (end == key || *end != '\0' || errno == ERANGE) {
        numeric = 0;
      }
    }
    if (numeric) {
      if (!bx_database_add_param_uint64(query, ":id", &id)) {
        bx_database_free_query(query);
        return ErrorGeneric;
      }
    } else if (!bx_database_add_param_char(query, ":id", (char *)key,
                                           strlen(key))) {
      bx_database_free_query(query);
      return ErrorGeneric;
    }
  }
  if (!bx_database_execute(query) || !bx_database_results(query)) {
    BXillError e = query->need_reconnect ? ErrorSQLReconnect : ErrorGeneric;
    bx_database_free_query(query);
    return e;
  }
  bx_database_free_query(query);
  return NoError;
}

BXillError bx_sync_one_key(bXill *app, MYSQL *conn, const char *table,
                           const char *key) {
  BXUInteger id;
  BXBool show_archived;
  BXillError err = NoError;
  Cache *cache = NULL;
  int seeded = 0;

  if (app == NULL || conn == NULL || table == NULL) {
    return ErrorGeneric;
  }
  if (strcmp(table, "company_profile") != 0 && key_missing(key)) {
    return ErrorGeneric;
  }
  if (strcmp(table, "contact") == 0 || strcmp(table, "invoice") == 0 ||
      strcmp(table, "pr_project") == 0) {
    err = seed_cache(conn, table, key_uint(key).value, &cache);
    if (err != NoError) {
      return err;
    }
    seeded = 1;
  }
  if (strcmp(table, "contact") == 0) {
    id = key_uint(key);
    memset(&show_archived, 0, sizeof(show_archived));
    show_archived.type = BX_OBJECT_TYPE_BOOL;
    show_archived.isset = true;
    /* Bexio show_archived=true returns archived contacts only. */
    show_archived.value = false;
    err = bx_contact_sync_item(app, conn, (BXGeneric *)&id, show_archived,
                               cache);
    if (err == ErrorNotFound) {
      show_archived.value = true;
      err = bx_contact_sync_item(app, conn, (BXGeneric *)&id, show_archived,
                                 cache);
    }
  } else if (strcmp(table, "invoice") == 0) {
    id = key_uint(key);
    err = bx_invoice_sync_item(app, conn, (BXGeneric *)&id, cache);
  } else if (strcmp(table, "pr_project") == 0) {
    id = key_uint(key);
    err = bx_project_sync_item(app, conn, (BXGeneric *)&id, cache);
  } else if (seeded) {
    err = ErrorGeneric;
  } else if (strcmp(table, "user") == 0) {
    id = key_uint(key);
    err = bx_user_sync_item(app, conn, (BXGeneric *)&id);
  } else if (strcmp(table, "contact_group") == 0) {
    id = key_uint(key);
    err = bx_contact_group_sync_item(app, conn, (BXGeneric *)&id);
  } else if (strcmp(table, "account") == 0) {
    err = bx_account_sync_id(app, conn, key_uint(key).value);
  } else if (strcmp(table, "currency") == 0) {
    err = bx_currency_sync_id(app, conn, key_uint(key).value);
  } else if (strcmp(table, "taxes") == 0) {
    err = bx_taxes_sync_id(app, conn, key_uint(key).value);
  } else if (strcmp(table, "bx_file") == 0) {
    err = bx_file_sync_id(app, conn, key_uint(key).value);
  } else if (strcmp(table, "language") == 0) {
    err = bx_language_load(app, conn);
  } else if (strcmp(table, "contact_sector") == 0) {
    err = bx_contact_sector_walk_items(app, conn);
  } else if (strcmp(table, "unit") == 0 || strcmp(table, "salutation") == 0 ||
             strcmp(table, "title") == 0 || strcmp(table, "payment_type") == 0 ||
             strcmp(table, "communication_kind") == 0 ||
             strcmp(table, "pr_project_state") == 0 ||
             strcmp(table, "pr_project_type") == 0 ||
             strcmp(table, "timesheet_status") == 0 ||
             strcmp(table, "todo_status") == 0 ||
             strcmp(table, "todo_priority") == 0 || strcmp(table, "stock") == 0 ||
             strcmp(table, "stock_place") == 0) {
    err = bx_named_sync_id(app, conn, table, key_uint(key).value);
  } else {
    err = bx_sync_more_one(app, conn, table, key);
  }
  if (cache != NULL) {
    cache_destroy(cache);
  }
  if (err == NoError) {
    BXillError stamped = touch_updated(conn, table, key);
    if (stamped != NoError) {
      return stamped;
    }
  }
  return err;
}

BXillError bx_sync_one(bXill *app, MYSQL *conn, const char *table, uint64_t id) {
  char key[32];
  snprintf(key, sizeof(key), "%llu", (unsigned long long)id);
  return bx_sync_one_key(app, conn, table, key);
}
