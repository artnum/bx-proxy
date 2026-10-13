#include "route.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define P(ver, res, table, del, arch, single, unpaged, strid)                  \
  {                                                                            \
    ver, res, table, NULL, NULL, NULL, NULL, del, arch, single, unpaged, strid \
  }

#define C(ver, res, table, child, ctable, col, doc, del)                       \
  { ver, res, table, child, ctable, col, doc, del, 0, 0, 0, 0 }

static const BxRoute routes[] = {
    P("2.0", "contact", "contact", 0, 1, 0, 0, 0),
    P("2.0", "contact_group", "contact_group", 1, 0, 0, 0, 0),
    P("2.0", "contact_branch", "contact_sector", 1, 0, 0, 0, 0),
    P("2.0", "kb_invoice", "invoice", 0, 0, 0, 0, 0),
    P("2.0", "kb_offer", "kb_quote", 1, 0, 0, 0, 0),
    P("2.0", "kb_order", "kb_order", 1, 0, 0, 0, 0),
    P("2.0", "kb_delivery", "kb_delivery", 1, 0, 0, 0, 0),
    P("2.0", "pr_project", "pr_project", 0, 0, 0, 0, 0),
    P("2.0", "unit", "unit", 1, 0, 0, 0, 0),
    P("2.0", "salutation", "salutation", 1, 0, 0, 0, 0),
    P("2.0", "title", "title", 1, 0, 0, 0, 0),
    P("2.0", "payment_type", "payment_type", 1, 0, 0, 0, 0),
    P("2.0", "communication_kind", "communication_kind", 1, 0, 0, 0, 0),
    P("2.0", "pr_project_state", "pr_project_state", 1, 0, 0, 0, 0),
    P("2.0", "pr_project_type", "pr_project_type", 1, 0, 0, 0, 0),
    P("2.0", "timesheet_status", "timesheet_status", 1, 0, 0, 0, 0),
    P("2.0", "todo_status", "todo_status", 1, 0, 0, 0, 0),
    P("2.0", "todo_priority", "todo_priority", 1, 0, 0, 0, 0),
    P("2.0", "stock", "stock", 1, 0, 0, 0, 0),
    P("2.0", "stock_place", "stock_place", 1, 0, 0, 0, 0),
    P("2.0", "language", "language", 1, 0, 0, 0, 0),
    P("2.0", "country", "country", 1, 0, 0, 0, 0),
    P("2.0", "account_groups", "account_group", 1, 0, 0, 0, 0),
    P("2.0", "accounts", "account", 1, 0, 0, 0, 0),
    P("2.0", "client_service", "client_service", 1, 0, 0, 0, 0),
    P("2.0", "company_profile", "company_profile", 1, 0, 1, 1, 0),
    P("2.0", "contact_relation", "contact_relation", 1, 0, 0, 0, 0),
    P("2.0", "article", "article", 1, 0, 0, 0, 0),
    P("2.0", "note", "note", 1, 0, 0, 0, 0),
    P("2.0", "task", "task", 1, 0, 0, 0, 0),
    P("2.0", "timesheet", "timesheet", 1, 0, 0, 0, 0),
    P("3.0", "currencies", "currency", 1, 0, 0, 0, 0),
    P("3.0", "users", "user", 1, 0, 0, 0, 0),
    P("3.0", "fictional_users", "user", 1, 0, 0, 0, 0),
    P("3.0", "files", "bx_file", 1, 0, 0, 0, 0),
    P("3.0", "taxes", "taxes", 0, 0, 0, 0, 0),
    P("3.0", "banking/accounts", "bank_account", 1, 0, 0, 0, 0),
    P("3.0", "accounting/calendar_years", "calendar_year", 1, 0, 0, 0, 0),
    P("3.0", "accounting/business_years", "business_year", 1, 0, 0, 1, 0),
    P("3.0", "accounting/vat_periods", "vat_period", 1, 0, 0, 1, 0),
    P("3.0", "accounting/manual_entries", "manual_entry", 1, 0, 0, 0, 0),
    P("3.0", "projects", "pr_project", 0, 0, 0, 0, 0),
    P("3.0", "purchase_orders", "purchase_order", 1, 0, 0, 0, 0),
    P("4.0", "purchase/bills", "purchase_bill", 1, 0, 0, 0, 1),
    P("4.0", "expenses", "expense", 1, 0, 0, 0, 1),
    C("2.0", "contact", "contact", "additional_address", "additional_address",
      "_contact_id", NULL, 1),
    C("2.0", "contact", "contact", "contact_relation", "contact_relation",
      "contact_id", NULL, 1),
    C("2.0", "kb_invoice", "invoice", "payment", "invoice_payment",
      "_invoice_id", NULL, 1),
    C("2.0", "kb_invoice", "invoice", "kb_reminder", "invoice_reminder",
      "_invoice_id", NULL, 1),
    C("2.0", "kb_invoice", "invoice", "comment", "kb_comment", "_document_id",
      "kb_invoice", 1),
    C("2.0", "kb_invoice", "invoice", "position", "invoice_position",
      "_invoice_id", NULL, 0),
    C("2.0", "kb_offer", "kb_quote", "comment", "kb_comment", "_document_id",
      "kb_offer", 1),
    C("2.0", "kb_offer", "kb_quote", "position", "quote_position", "_quote_id",
      NULL, 0),
    C("2.0", "kb_order", "kb_order", "comment", "kb_comment", "_document_id",
      "kb_order", 1),
    C("2.0", "kb_order", "kb_order", "position", "order_position", "_order_id",
      NULL, 0),
    C("2.0", "kb_delivery", "kb_delivery", "comment", "kb_comment",
      "_document_id", "kb_delivery", 1),
    C("3.0", "projects", "pr_project", "milestones", "project_milestone",
      "_project_id", NULL, 1),
    C("3.0", "projects", "pr_project", "packages", "project_package",
      "_project_id", NULL, 1),
};

static const BxRoute *parent_route(const char *version, const char *resource) {
  size_t i;
  for (i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    if (routes[i].child == NULL && strcmp(routes[i].version, version) == 0 &&
        strcmp(routes[i].resource, resource) == 0) {
      return &routes[i];
    }
  }
  return NULL;
}

static int has_longer(const char *version, const char *resource) {
  char prefix[100];
  size_t n;
  size_t i;
  n = (size_t)snprintf(prefix, sizeof(prefix), "%s/", resource);
  if (n >= sizeof(prefix)) {
    return 0;
  }
  for (i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    if (routes[i].child == NULL && strcmp(routes[i].version, version) == 0 &&
        strncmp(routes[i].resource, prefix, n) == 0) {
      return 1;
    }
  }
  return 0;
}

const BxRoute *bx_route_lookup(const BxRequest *req) {
  size_t i;
  if (req == NULL || req->status != 0 || req->version[0] == '\0' ||
      req->resource[0] == '\0') {
    return NULL;
  }
  for (i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    if (strcmp(routes[i].version, req->version) != 0 ||
        strcmp(routes[i].resource, req->resource) != 0) {
      continue;
    }
    if (req->child[0] == '\0') {
      if (routes[i].child == NULL) {
        return &routes[i];
      }
    } else if (routes[i].child != NULL &&
               strcmp(routes[i].child, req->child) == 0) {
      return &routes[i];
    }
  }
  return NULL;
}

static int digits_id(const char *s, size_t n, uint64_t *out) {
  char buf[32];
  char *end = NULL;
  unsigned long long v;
  if (n == 0 || n >= sizeof(buf)) {
    return 0;
  }
  memcpy(buf, s, n);
  buf[n] = '\0';
  errno = 0;
  v = strtoull(buf, &end, 10);
  if (end != buf + n || errno == ERANGE) {
    return 0;
  }
  *out = (uint64_t)v;
  return 1;
}

static int string_id_ok(const char *s, size_t n) {
  size_t i;
  if (n == 0 || n > 64) {
    return 0;
  }
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    if (!isalnum(c) && c != '-' && c != '_') {
      return 0;
    }
  }
  return 1;
}

static int token_is_id(const BxRoute *parent, const char *s, size_t n) {
  uint64_t ignore;
  if (parent->string_id) {
    return string_id_ok(s, n);
  }
  return digits_id(s, n, &ignore);
}

static int parse_query(const char *query, BxRequest *out) {
  int offset_set = 0;
  int page = -1;
  const char *p = query;
  if (query == NULL || query[0] == '\0') {
    return 0;
  }
  while (*p) {
    const char *eq;
    const char *amp;
    size_t klen;
    size_t vlen;
    char key[32];
    char val[32];
    if (*p == '&') {
      p++;
      continue;
    }
    eq = strchr(p, '=');
    amp = strchr(p, '&');
    if (eq == NULL || (amp != NULL && amp < eq)) {
      return -1;
    }
    klen = (size_t)(eq - p);
    vlen = amp == NULL ? strlen(eq + 1) : (size_t)(amp - (eq + 1));
    if (klen == 0 || klen >= sizeof(key) || vlen >= sizeof(val)) {
      return -1;
    }
    memcpy(key, p, klen);
    key[klen] = '\0';
    memcpy(val, eq + 1, vlen);
    val[vlen] = '\0';
    if (strcmp(key, "limit") == 0) {
      char *end = NULL;
      long v = strtol(val, &end, 10);
      if (end == val || *end != '\0' || v < 1) {
        return -1;
      }
      out->limit = v > 500 ? 500 : (int)v;
    } else if (strcmp(key, "offset") == 0) {
      char *end = NULL;
      long v = strtol(val, &end, 10);
      if (end == val || *end != '\0' || v < 0) {
        return -1;
      }
      out->offset = (int)v;
      offset_set = 1;
    } else if (strcmp(key, "page") == 0) {
      char *end = NULL;
      long v = strtol(val, &end, 10);
      if (end == val || *end != '\0' || v < 1) {
        return -1;
      }
      page = (int)v;
    } else if (strcmp(key, "show_archived") == 0) {
      if (strcasecmp(val, "1") == 0 || strcasecmp(val, "true") == 0 ||
          strcasecmp(val, "yes") == 0) {
        out->show_archived = 1;
      } else if (strcasecmp(val, "0") == 0 || strcasecmp(val, "false") == 0 ||
                 strcasecmp(val, "no") == 0) {
        out->show_archived = 0;
      } else {
        return -1;
      }
    }
    if (amp == NULL) {
      break;
    }
    p = amp + 1;
  }
  if (!offset_set && page > 0) {
    out->offset = (page - 1) * out->limit;
  }
  return 0;
}

int bx_parse_request(const char *path, const char *query, BxRequest *out) {
  char copy[512];
  const char *cursor;
  struct {
    const char *s;
    size_t n;
  } segs[8];
  int nseg = 0;
  char resource[96];
  int k;
  size_t i;

  if (out == NULL || path == NULL) {
    return 400;
  }
  memset(out, 0, sizeof(*out));
  out->show_archived = -1;
  out->limit = 500;
  if (path[0] == '\0' || strlen(path) >= sizeof(copy) || strchr(path, '%')) {
    out->status = 400;
    return 0;
  }
  memcpy(copy, path, strlen(path) + 1);
  cursor = copy;
  if (*cursor == '/') {
    cursor++;
  }
  if (*cursor == '\0') {
    out->status = 400;
    return 0;
  }
  while (*cursor && nseg < 8) {
    const char *slash = strchr(cursor, '/');
    size_t n = slash == NULL ? strlen(cursor) : (size_t)(slash - cursor);
    if (n == 0) {
      out->status = 400;
      return 0;
    }
    segs[nseg].s = cursor;
    segs[nseg].n = n;
    nseg++;
    if (slash == NULL) {
      break;
    }
    cursor = slash + 1;
    if (*cursor == '\0') {
      out->status = 400;
      return 0;
    }
  }
  if (nseg == 8 && strchr(cursor, '/')) {
    out->status = 400;
    return 0;
  }
  if (segs[0].n == 0 || segs[0].n >= sizeof(out->version)) {
    out->status = 400;
    return 0;
  }
  memcpy(out->version, segs[0].s, segs[0].n);
  out->version[segs[0].n] = '\0';
  resource[0] = '\0';
  k = 1;
  while (k < nseg) {
    char trial[96];
    const BxRoute *parent;
    int wr;
    if (resource[0] == '\0') {
      wr = snprintf(trial, sizeof(trial), "%.*s", (int)segs[k].n, segs[k].s);
    } else {
      wr = snprintf(trial, sizeof(trial), "%s/%.*s", resource, (int)segs[k].n,
                    segs[k].s);
    }
    if (wr < 0 || (size_t)wr >= sizeof(trial)) {
      out->status = 400;
      return 0;
    }
    parent = parent_route(out->version, trial);
    if (parent != NULL &&
        (k + 1 == nseg || token_is_id(parent, segs[k + 1].s, segs[k + 1].n))) {
      memcpy(resource, trial, (size_t)wr + 1);
      k++;
      break;
    }
    if (parent != NULL && !has_longer(out->version, trial)) {
      out->status = 400;
      return 0;
    }
    memcpy(resource, trial, (size_t)wr + 1);
    k++;
  }
  if (strlen(resource) >= sizeof(out->resource)) {
    out->status = 400;
    return 0;
  }
  memcpy(out->resource, resource, strlen(resource) + 1);
  if (k < nseg) {
    const BxRoute *parent = parent_route(out->version, out->resource);
    if (parent == NULL) {
      out->status = 400;
      return 0;
    }
    if (parent->string_id) {
      if (!string_id_ok(segs[k].s, segs[k].n)) {
        out->status = 400;
        return 0;
      }
      memcpy(out->id_str, segs[k].s, segs[k].n);
      out->id_str[segs[k].n] = '\0';
      out->has_id = 1;
      out->id_is_string = 1;
    } else {
      if (!digits_id(segs[k].s, segs[k].n, &out->id)) {
        out->status = 400;
        return 0;
      }
      snprintf(out->id_str, sizeof(out->id_str), "%llu",
               (unsigned long long)out->id);
      out->has_id = 1;
    }
    k++;
    if (k < nseg) {
      if (k + 1 != nseg || segs[k].n == 0 ||
          segs[k].n >= sizeof(out->child)) {
        out->status = 400;
        return 0;
      }
      for (i = 0; i < segs[k].n; i++) {
        unsigned char c = (unsigned char)segs[k].s[i];
        if (!isalnum(c) && c != '_' && c != '-') {
          out->status = 400;
          return 0;
        }
      }
      memcpy(out->child, segs[k].s, segs[k].n);
      out->child[segs[k].n] = '\0';
    }
  }
  if (parse_query(query, out) != 0) {
    out->status = 400;
  }
  return 0;
}
