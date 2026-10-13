#include "serve.h"

#include "fresh.h"
#include "read_json.h"
#include "route.h"
#include "bx_conf.h"
#include "bx_database.h"
#include "bx_sync_one.h"
#include "bx_utils.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct BxServe {
  bXill *app;
  MYSQL *conn;
  int stale_seconds;
  int need_reconnect;
};

BxServe *bx_serve_new(bXill *app, int stale_seconds) {
  BxServe *serve = calloc(1, sizeof(*serve));
  if (serve == NULL) {
    return NULL;
  }
  serve->app = app;
  if (stale_seconds <= 0) {
    stale_seconds = 7200;
  }
  serve->stale_seconds = stale_seconds;
  return serve;
}

void bx_serve_free(BxServe *serve) {
  if (serve == NULL) {
    return;
  }
  bx_serve_close(serve);
  free(serve);
}

int bx_serve_connect(BxServe *serve) {
  MYSQL *conn = NULL;
  if (serve == NULL || serve->app == NULL) {
    return -1;
  }
  mysql_thread_init();
  conn = mysql_init(NULL);
  if (conn == NULL) {
    mysql_thread_end();
    return -1;
  }
  if (!mysql_real_connect(conn, bx_conf_get_string(serve->app->conf, "mysql-host"),
                          bx_conf_get_string(serve->app->conf, "mysql-user"),
                          bx_conf_get_string(serve->app->conf, "mysql-password"),
                          bx_conf_get_string(serve->app->conf, "mysql-database"),
                          0, NULL, 0)) {
    bx_log_error("MySQL connect failed: %s", mysql_error(conn));
    mysql_close(conn);
    mysql_thread_end();
    serve->conn = NULL;
    return -1;
  }
  mysql_set_character_set(conn, "utf8mb4");
  serve->conn = conn;
  serve->need_reconnect = 0;
  return 0;
}

void bx_serve_close(BxServe *serve) {
  if (serve == NULL || serve->conn == NULL) {
    return;
  }
  mysql_close(serve->conn);
  serve->conn = NULL;
  mysql_thread_end();
}

void bx_serve_reconnect(BxServe *serve) {
  if (serve == NULL) {
    return;
  }
  bx_log_info("Reconnect MySQL");
  bx_serve_close(serve);
  bx_serve_connect(serve);
}

void bx_serve_recover(BxServe *serve) {
  if (serve != NULL && serve->need_reconnect) {
    bx_serve_reconnect(serve);
  }
}

static char *error_json(const char *msg) {
  json_t *obj = json_object();
  char *text = NULL;
  if (obj == NULL) {
    return NULL;
  }
  json_object_set_new(obj, "error", json_string(msg));
  text = json_dumps(obj, JSON_COMPACT);
  json_decref(obj);
  return text;
}

static void set_proxy(char *proxy, size_t proxy_len, const char *value) {
  if (proxy == NULL || proxy_len == 0) {
    return;
  }
  snprintf(proxy, proxy_len, "%s", value != NULL ? value : "");
}

static int ident_ok(const char *name) {
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

static int fail(int *status, char **body, char *proxy, size_t proxy_len,
                int code, const char *msg) {
  set_proxy(proxy, proxy_len, "");
  *status = code;
  *body = error_json(msg);
  return *body != NULL ? 0 : -1;
}

static void row_stamp(BXDatabaseQuery *query, int *present, uint64_t *updated,
                      int *deleted) {
  BXDatabaseRow *row;
  int i;
  *present = 0;
  *updated = 0;
  *deleted = 0;
  if (query == NULL || query->row_count <= 0 || query->results == NULL) {
    return;
  }
  row = query->results;
  *present = 1;
  for (i = 0; i < row->column_count; i++) {
    const char *name = bx_database_get_column_name(query, &row->columns[i]);
    if (name == NULL || row->columns[i].is_null) {
      continue;
    }
    if (strcmp(name, "_last_updated") == 0) {
      *updated = (uint64_t)row->columns[i].i_value;
    } else if (strcmp(name, "_deleted") == 0) {
      *deleted = row->columns[i].i_value != 0;
    }
  }
}

static int bind_id(BXDatabaseQuery *query, const BxRequest *req) {
  if (req->id_is_string) {
    return bx_database_add_param_char(query, ":id", (char *)req->id_str,
                                      strlen(req->id_str))
               ? 1
               : 0;
  }
  return bx_database_add_param_uint64(query, ":id", (uint64_t *)&req->id) ? 1
                                                                          : 0;
}

static BXDatabaseQuery *run_query(BxServe *serve, const char *sql, int *status,
                                  char **body, char *proxy, size_t proxy_len) {
  BXDatabaseQuery *query = NULL;
  if (serve->conn == NULL) {
    serve->need_reconnect = 1;
    fail(status, body, proxy, proxy_len, 503, "database unavailable");
    return NULL;
  }
  query = bx_database_new_query(serve->conn, sql);
  if (query == NULL) {
    serve->need_reconnect = 1;
    fail(status, body, proxy, proxy_len, 503, "database unavailable");
    return NULL;
  }
  return query;
}

static int exec_query(BxServe *serve, BXDatabaseQuery *query, int *status,
                      char **body, char *proxy, size_t proxy_len) {
  if (!bx_database_execute(query) || !bx_database_results(query)) {
    int reconnect = query->need_reconnect;
    bx_database_free_query(query);
    if (reconnect) {
      serve->need_reconnect = 1;
      return fail(status, body, proxy, proxy_len, 503, "database unavailable");
    }
    return fail(status, body, proxy, proxy_len, 500, "database error");
  }
  return 1;
}

static int answer_rows(BXDatabaseQuery *query, int one_object, int *status,
                       char **body, char *proxy, size_t proxy_len,
                       const char *which) {
  char *text = NULL;
  if (one_object) {
    if (query->row_count <= 0 || query->results == NULL) {
      bx_database_free_query(query);
      return fail(status, body, proxy, proxy_len, 404, "not found");
    }
    text = bx_row_to_json(query, query->results);
  } else {
    text = bx_rows_to_json(query);
  }
  bx_database_free_query(query);
  if (text == NULL) {
    return fail(status, body, proxy, proxy_len, 500, "encode failed");
  }
  *status = 200;
  *body = text;
  set_proxy(proxy, proxy_len, which);
  return 0;
}

static int detail_sql(char *sql, size_t n, const char *table) {
  return snprintf(sql, n, "SELECT * FROM `%s` WHERE `id` = :id", table);
}

static int list_sql(char *sql, size_t n, const BxRoute *route,
                    const BxRequest *req) {
  const char *where = "";
  if (route->has_deleted && route->archived_filter && req->show_archived != 1) {
    where = " WHERE _deleted = 0 AND _archived = 0";
  } else if (route->has_deleted) {
    where = " WHERE _deleted = 0";
  } else if (route->archived_filter && req->show_archived != 1) {
    where = " WHERE _archived = 0";
  }
  if (route->unpaged) {
    return snprintf(sql, n, "SELECT * FROM `%s`%s ORDER BY id", route->table,
                    where);
  }
  return snprintf(sql, n, "SELECT * FROM `%s`%s ORDER BY id LIMIT %d OFFSET %d",
                  route->table, where, req->limit, req->offset);
}

static int child_sql(char *sql, size_t n, const BxRoute *route,
                     const BxRequest *req) {
  const char *deleted = route->has_deleted ? " AND _deleted = 0" : "";
  if (route->doc_type != NULL) {
    return snprintf(sql, n,
                    "SELECT * FROM `%s` WHERE `%s` = :id AND `_document_type` "
                    "= :doc%s ORDER BY id LIMIT %d OFFSET %d",
                    route->child_table, route->parent_column, deleted,
                    req->limit, req->offset);
  }
  return snprintf(sql, n,
                  "SELECT * FROM `%s` WHERE `%s` = :id%s ORDER BY id LIMIT %d "
                  "OFFSET %d",
                  route->child_table, route->parent_column, deleted, req->limit,
                  req->offset);
}

static BXDatabaseQuery *select_bound(BxServe *serve, const char *sql,
                                     const BxRequest *req, const char *doc,
                                     int *status, char **body, char *proxy,
                                     size_t proxy_len) {
  BXDatabaseQuery *query = run_query(serve, sql, status, body, proxy, proxy_len);
  if (query == NULL) {
    return NULL;
  }
  if (!bind_id(query, req) ||
      (doc != NULL &&
       !bx_database_add_param_char(query, ":doc", (char *)doc, strlen(doc)))) {
    bx_database_free_query(query);
    fail(status, body, proxy, proxy_len, 500, "database error");
    return NULL;
  }
  if (exec_query(serve, query, status, body, proxy, proxy_len) != 1) {
    return NULL;
  }
  return query;
}

static int refresh_parent(BxServe *serve, const char *table, const char *key,
                          int had_live, char *stale_body, int *status,
                          char **body, char *proxy, size_t proxy_len) {
  BXillError err = bx_sync_one_key(serve->app, serve->conn, table, key);
  if (err == ErrorSQLReconnect) {
    serve->need_reconnect = 1;
    free(stale_body);
    return fail(status, body, proxy, proxy_len, 503, "database unavailable");
  }
  if (err == ErrorNotFound) {
    free(stale_body);
    return fail(status, body, proxy, proxy_len, 404, "not found");
  }
  if (err != NoError) {
    if (had_live && stale_body != NULL) {
      *status = 200;
      *body = stale_body;
      set_proxy(proxy, proxy_len, "stale");
      return 0;
    }
    free(stale_body);
    return fail(status, body, proxy, proxy_len, 502, "upstream failed");
  }
  free(stale_body);
  return 1;
}

int bx_serve_request(BxServe *serve, const char *path, const char *query,
                     int is_get, int *status, char **body, char *proxy,
                     size_t proxy_len) {
  BxRequest req;
  const BxRoute *route;
  char sql[512];
  int present = 0;
  int deleted = 0;
  uint64_t updated = 0;
  BxAge age;
  int had_live = 0;
  char *stale_body = NULL;
  BXDatabaseQuery *rows = NULL;
  int refreshed = 0;

  if (status == NULL || body == NULL) {
    return -1;
  }
  *body = NULL;
  *status = 500;
  set_proxy(proxy, proxy_len, "");
  if (serve == NULL) {
    return fail(status, body, proxy, proxy_len, 500, "encode failed");
  }
  memset(&req, 0, sizeof(req));
  if (bx_parse_request(path, query, &req) != 0 || req.status != 0) {
    return fail(status, body, proxy, proxy_len, 400, "bad request");
  }
  route = bx_route_lookup(&req);
  if (route == NULL) {
    return fail(status, body, proxy, proxy_len, 404, "not found");
  }
  if (!is_get) {
    return fail(status, body, proxy, proxy_len, 405, "method not allowed");
  }
  if (!ident_ok(route->table) ||
      (route->child_table != NULL && !ident_ok(route->child_table)) ||
      (route->parent_column != NULL && !ident_ok(route->parent_column))) {
    return fail(status, body, proxy, proxy_len, 500, "database error");
  }

  if (!req.has_id && route->child == NULL && !route->singleton) {
    if (list_sql(sql, sizeof(sql), route, &req) < 0 ||
        (size_t)strlen(sql) >= sizeof(sql) - 1) {
      return fail(status, body, proxy, proxy_len, 500, "database error");
    }
    rows = run_query(serve, sql, status, body, proxy, proxy_len);
    if (rows == NULL) {
      return *body != NULL ? 0 : -1;
    }
    if (exec_query(serve, rows, status, body, proxy, proxy_len) != 1) {
      return *body != NULL ? 0 : -1;
    }
    return answer_rows(rows, 0, status, body, proxy, proxy_len, "hit");
  }

  if (route->child != NULL) {
    if (detail_sql(sql, sizeof(sql), route->table) < 0) {
      return fail(status, body, proxy, proxy_len, 500, "database error");
    }
    rows = select_bound(serve, sql, &req, NULL, status, body, proxy, proxy_len);
    if (rows == NULL) {
      return *body != NULL ? 0 : -1;
    }
    row_stamp(rows, &present, &updated, &deleted);
    age = bx_age(present, updated, deleted, time(NULL), serve->stale_seconds);
    had_live = (age == BX_STALE);
    bx_database_free_query(rows);
    rows = NULL;
    if (age != BX_FRESH) {
      BXillError err =
          bx_sync_one_key(serve->app, serve->conn, route->table, req.id_str);
      if (err == ErrorSQLReconnect) {
        serve->need_reconnect = 1;
        return fail(status, body, proxy, proxy_len, 503, "database unavailable");
      }
      if (err == ErrorNotFound) {
        return fail(status, body, proxy, proxy_len, 404, "not found");
      }
      if (err != NoError) {
        if (!had_live) {
          return fail(status, body, proxy, proxy_len, 502, "upstream failed");
        }
      } else {
        rows = select_bound(serve, sql, &req, NULL, status, body, proxy,
                            proxy_len);
        if (rows == NULL) {
          return *body != NULL ? 0 : -1;
        }
        row_stamp(rows, &present, &updated, &deleted);
        bx_database_free_query(rows);
        rows = NULL;
        if (!present || deleted) {
          return fail(status, body, proxy, proxy_len, 404, "not found");
        }
        refreshed = 1;
      }
    }
    if (child_sql(sql, sizeof(sql), route, &req) < 0 ||
        strlen(sql) >= sizeof(sql) - 1) {
      return fail(status, body, proxy, proxy_len, 500, "database error");
    }
    rows = select_bound(serve, sql, &req, route->doc_type, status, body, proxy,
                        proxy_len);
    if (rows == NULL) {
      return *body != NULL ? 0 : -1;
    }
    return answer_rows(rows, 0, status, body, proxy, proxy_len,
                       refreshed ? "refresh"
                                 : (had_live && age != BX_FRESH ? "stale" : "hit"));
  }

  if (req.has_id) {
    if (detail_sql(sql, sizeof(sql), route->table) < 0) {
      return fail(status, body, proxy, proxy_len, 500, "database error");
    }
  } else if (list_sql(sql, sizeof(sql), route, &req) < 0) {
    return fail(status, body, proxy, proxy_len, 500, "database error");
  }
  rows = req.has_id ? select_bound(serve, sql, &req, NULL, status, body, proxy,
                                   proxy_len)
                    : run_query(serve, sql, status, body, proxy, proxy_len);
  if (rows == NULL) {
    return *body != NULL ? 0 : -1;
  }
  if (!req.has_id &&
      exec_query(serve, rows, status, body, proxy, proxy_len) != 1) {
    return *body != NULL ? 0 : -1;
  }
  row_stamp(rows, &present, &updated, &deleted);
  age = bx_age(present, updated, deleted, time(NULL), serve->stale_seconds);
  if (age == BX_FRESH) {
    return answer_rows(rows, 1, status, body, proxy, proxy_len, "hit");
  }
  had_live = (age == BX_STALE);
  if (had_live) {
    stale_body = bx_row_to_json(rows, rows->results);
  }
  bx_database_free_query(rows);
  {
    int again = refresh_parent(serve, route->table, req.has_id ? req.id_str : NULL,
                               had_live, stale_body, status, body, proxy,
                               proxy_len);
    if (again != 1) {
      return again;
    }
  }
  rows = req.has_id ? select_bound(serve, sql, &req, NULL, status, body, proxy,
                                   proxy_len)
                    : run_query(serve, sql, status, body, proxy, proxy_len);
  if (rows == NULL) {
    return *body != NULL ? 0 : -1;
  }
  if (!req.has_id &&
      exec_query(serve, rows, status, body, proxy, proxy_len) != 1) {
    return *body != NULL ? 0 : -1;
  }
  row_stamp(rows, &present, &updated, &deleted);
  if (!present || deleted) {
    bx_database_free_query(rows);
    return fail(status, body, proxy, proxy_len, 404, "not found");
  }
  return answer_rows(rows, 1, status, body, proxy, proxy_len, "refresh");
}
