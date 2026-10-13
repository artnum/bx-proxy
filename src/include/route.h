#ifndef BX_PROXY_ROUTE_H__
#define BX_PROXY_ROUTE_H__

#include <stddef.h>
#include <stdint.h>

typedef struct BxRequest {
  char version[8];
  char resource[96];
  int has_id;
  int id_is_string;
  uint64_t id;
  char id_str[80];
  char child[64];
  int limit;
  int offset;
  int show_archived;
  int status;
} BxRequest;

typedef struct BxRoute {
  const char *version;
  const char *resource;
  const char *table;
  const char *child;
  const char *child_table;
  const char *parent_column;
  const char *doc_type;
  int has_deleted;
  int archived_filter;
  int singleton;
  int unpaged;
  int string_id;
} BxRoute;

/* 0 and req->status set, or 400 when req is NULL or path is NULL. */
int bx_parse_request(const char *path, const char *query, BxRequest *out);
const BxRoute *bx_route_lookup(const BxRequest *req);

#endif
