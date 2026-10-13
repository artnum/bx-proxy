#include "route.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void check(int cond, const char *file, int line, const char *what) {
  if (!cond) {
    fprintf(stderr, "FAIL %s:%d %s\n", file, line, what);
    fails++;
  }
}

#define CHECK(cond) check((cond), __FILE__, __LINE__, #cond)

static void parse_ok(const char *path, const char *query, BxRequest *req) {
  memset(req, 0x5a, sizeof(*req));
  int st = bx_parse_request(path, query, req);
  CHECK(st == 0);
  CHECK(req->status == 0);
}

int main(void) {
  BxRequest req;
  const BxRoute *route;

  parse_ok("/2.0/contact/123", "limit=10&offset=2", &req);
  CHECK(strcmp(req.version, "2.0") == 0);
  CHECK(strcmp(req.resource, "contact") == 0);
  CHECK(req.has_id == 1);
  CHECK(req.id_is_string == 0);
  CHECK(req.id == 123);
  CHECK(req.child[0] == '\0');
  CHECK(req.limit == 10);
  CHECK(req.offset == 2);
  CHECK(req.show_archived == -1);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "contact") == 0);
  CHECK(route->child == NULL);
  CHECK(route->archived_filter == 1);

  parse_ok("/2.0/contact", "limit=9000", &req);
  CHECK(req.has_id == 0);
  CHECK(req.limit == 500);
  CHECK(req.offset == 0);

  parse_ok("/2.0/contact", NULL, &req);
  CHECK(req.limit == 500);
  CHECK(req.offset == 0);

  CHECK(bx_parse_request("/2.0/contact/12abc", NULL, &req) == 0);
  CHECK(req.status == 400);

  CHECK(bx_parse_request("/2.0/contact", "limit=0", &req) == 0);
  CHECK(req.status == 400);

  CHECK(bx_parse_request("/2.0/contact", "offset=-1", &req) == 0);
  CHECK(req.status == 400);

  parse_ok("/2.0/contact", "page=3&limit=10", &req);
  CHECK(req.offset == 20);
  CHECK(req.limit == 10);

  parse_ok("/2.0/contact", "offset=5&page=3&limit=10", &req);
  CHECK(req.offset == 5);

  parse_ok("/2.0/contact", "show_archived=true", &req);
  CHECK(req.show_archived == 1);
  parse_ok("/2.0/contact", "show_archived=0", &req);
  CHECK(req.show_archived == 0);

  parse_ok("/2.0/kb_invoice/9/payment", NULL, &req);
  CHECK(req.id == 9);
  CHECK(strcmp(req.child, "payment") == 0);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "invoice") == 0);
  CHECK(strcmp(route->child_table, "invoice_payment") == 0);
  CHECK(strcmp(route->parent_column, "_invoice_id") == 0);

  parse_ok("/3.0/projects/4/milestones", NULL, &req);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "pr_project") == 0);
  CHECK(strcmp(route->child_table, "project_milestone") == 0);

  parse_ok("/2.0/contact_branch", NULL, &req);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "contact_sector") == 0);

  parse_ok("/3.0/currencies", NULL, &req);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "currency") == 0);

  parse_ok("/4.0/purchase/bills", NULL, &req);
  CHECK(strcmp(req.resource, "purchase/bills") == 0);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "purchase_bill") == 0);
  CHECK(route->string_id == 1);

  parse_ok("/4.0/purchase/bills/abc-def", NULL, &req);
  CHECK(req.has_id == 1);
  CHECK(req.id_is_string == 1);
  CHECK(strcmp(req.id_str, "abc-def") == 0);

  parse_ok("/2.0/kb_offer/3", NULL, &req);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "kb_quote") == 0);

  parse_ok("/3.0/fictional_users/2", NULL, &req);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "user") == 0);
  CHECK(req.id == 2);

  parse_ok("/3.0/banking/accounts/8", NULL, &req);
  route = bx_route_lookup(&req);
  CHECK(route != NULL);
  CHECK(strcmp(route->table, "bank_account") == 0);
  CHECK(req.id == 8);

  parse_ok("/2.0/no-such", NULL, &req);
  CHECK(bx_route_lookup(&req) == NULL);

  parse_ok("/2.0/kb_invoice/9/nope", NULL, &req);
  CHECK(bx_route_lookup(&req) == NULL);

  CHECK(bx_parse_request("/", NULL, &req) == 0);
  CHECK(req.status == 400);
  CHECK(bx_parse_request(NULL, NULL, &req) == 400);

  if (fails) {
    fprintf(stderr, "%d route checks failed\n", fails);
    return 1;
  }
  printf("route ok\n");
  return 0;
}
