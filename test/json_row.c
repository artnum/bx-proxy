#include "read_json.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void check(int cond, const char *file, int line, const char *what) {
  if (!cond) {
    fprintf(stderr, "FAIL %s:%d %s\n", file, line, what);
    fails++;
  }
}

#define CHECK(cond) check((cond), __FILE__, __LINE__, #cond)

int main(void) {
  BXDatabaseField fields[5];
  BXDatabaseColumn cols[5];
  BXDatabaseRow row;
  BXDatabaseQuery query;
  unsigned char uuid[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  char name[] = "Ada";
  char *text;
  json_t *got;
  json_t *want;
  json_error_t err;

  memset(&fields, 0, sizeof(fields));
  memset(&cols, 0, sizeof(cols));
  memset(&row, 0, sizeof(row));
  memset(&query, 0, sizeof(query));

  fields[0].name = "id";
  fields[0].id = 0;
  fields[1].name = "_checksum";
  fields[1].id = 1;
  fields[2].name = "_last_updated";
  fields[2].id = 2;
  fields[3].name = "name";
  fields[3].id = 3;
  fields[4].name = "uuid";
  fields[4].id = 4;

  cols[0].column = 0;
  cols[0].type = MYSQL_TYPE_LONGLONG;
  cols[0].i_value = 7;
  cols[1].column = 1;
  cols[1].type = MYSQL_TYPE_LONGLONG;
  cols[1].i_value = 99;
  cols[2].column = 2;
  cols[2].type = MYSQL_TYPE_LONGLONG;
  cols[2].i_value = 1;
  cols[3].column = 3;
  cols[3].type = MYSQL_TYPE_VAR_STRING;
  cols[3].c_value = name;
  cols[3].value_len = 3;
  cols[4].column = 4;
  cols[4].type = MYSQL_TYPE_BLOB;
  cols[4].c_value = (char *)uuid;
  cols[4].value_len = 16;

  row.column_count = 5;
  row.columns = cols;
  query.field_count = 5;
  query.fields = fields;
  query.results = &row;
  query.row_count = 1;

  text = bx_row_to_json(&query, &row);
  CHECK(text != NULL);
  CHECK(strstr(text, "_checksum") == NULL);
  CHECK(strstr(text, "_last_updated") == NULL);
  got = json_loads(text, 0, &err);
  want = json_loads(
      "{\"id\":7,\"name\":\"Ada\",\"uuid\":\"00112233-4455-6677-8899-aabbccddeeff\"}",
      0, &err);
  CHECK(got != NULL && want != NULL && json_equal(got, want));
  json_decref(got);
  json_decref(want);
  free(text);

  cols[3].is_null = true;
  text = bx_row_to_json(&query, &row);
  got = json_loads(text, 0, &err);
  CHECK(got != NULL);
  CHECK(json_is_null(json_object_get(got, "name")));
  json_decref(got);
  free(text);
  cols[3].is_null = false;

  query.results = NULL;
  query.row_count = 0;
  text = bx_rows_to_json(&query);
  CHECK(text != NULL && strcmp(text, "[]") == 0);
  free(text);

  {
    BXDatabaseRow row2;
    row2 = row;
    row.next = &row2;
    row2.next = NULL;
    query.results = &row;
    query.row_count = 2;
    text = bx_rows_to_json(&query);
    got = json_loads(text, 0, &err);
    CHECK(json_is_array(got) && json_array_size(got) == 2);
    json_decref(got);
    free(text);
    row.next = NULL;
  }

  CHECK(bx_row_to_json(NULL, NULL) == NULL);

  if (fails) {
    fprintf(stderr, "%d json checks failed\n", fails);
    return 1;
  }
  printf("json ok\n");
  return 0;
}
