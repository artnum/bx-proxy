#include "read_json.h"

#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *field_name(const BXDatabaseQuery *query, int column) {
  int i;
  if (query == NULL || query->fields == NULL) {
    return NULL;
  }
  for (i = 0; i < query->field_count; i++) {
    if (query->fields[i].id == column) {
      return query->fields[i].name;
    }
  }
  return NULL;
}

static void uuid_text(const unsigned char *b, char out[37]) {
  snprintf(out, 37,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
           b[11], b[12], b[13], b[14], b[15]);
}

static json_t *column_json(const char *name, const BXDatabaseColumn *col) {
  if (col->is_null) {
    return json_null();
  }
  switch (col->type) {
  case MYSQL_TYPE_TINY:
  case MYSQL_TYPE_SHORT:
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_LONGLONG:
  case MYSQL_TYPE_INT24:
  case MYSQL_TYPE_BIT:
    return json_integer((json_int_t)col->i_value);
  case MYSQL_TYPE_FLOAT:
  case MYSQL_TYPE_DOUBLE:
    return json_real(col->f_value);
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_DECIMAL:
    if (name != NULL && strcmp(name, "uuid") == 0 && col->value_len == 16 &&
        col->c_value != NULL) {
      char text[37];
      uuid_text((const unsigned char *)col->c_value, text);
      return json_string(text);
    }
    if (col->c_value == NULL) {
      return json_null();
    }
    return json_stringn(col->c_value, col->value_len);
  default:
    if (col->c_value != NULL) {
      return json_stringn(col->c_value, col->value_len);
    }
    return json_null();
  }
}

static json_t *row_object(const BXDatabaseQuery *query, const BXDatabaseRow *row) {
  json_t *obj;
  int i;
  if (query == NULL || row == NULL) {
    return NULL;
  }
  obj = json_object();
  if (obj == NULL) {
    return NULL;
  }
  for (i = 0; i < row->column_count; i++) {
    const char *name = field_name(query, row->columns[i].column);
    json_t *value;
    if (name == NULL || name[0] == '\0' || name[0] == '_') {
      continue;
    }
    value = column_json(name, &row->columns[i]);
    if (value == NULL || json_object_set_new(obj, name, value) != 0) {
      json_decref(value);
      json_decref(obj);
      return NULL;
    }
  }
  return obj;
}

char *bx_row_to_json(const BXDatabaseQuery *query, const BXDatabaseRow *row) {
  json_t *obj;
  char *text;
  if (query == NULL || row == NULL) {
    return NULL;
  }
  obj = row_object(query, row);
  if (obj == NULL) {
    return NULL;
  }
  text = json_dumps(obj, JSON_COMPACT);
  json_decref(obj);
  return text;
}

char *bx_rows_to_json(const BXDatabaseQuery *query) {
  json_t *arr;
  const BXDatabaseRow *row;
  int n = 0;
  char *text;
  if (query == NULL) {
    return NULL;
  }
  arr = json_array();
  if (arr == NULL) {
    return NULL;
  }
  for (row = query->results; row != NULL && n < query->row_count;
       row = row->next, n++) {
    json_t *obj = row_object(query, row);
    if (obj == NULL || json_array_append_new(arr, obj) != 0) {
      json_decref(obj);
      json_decref(arr);
      return NULL;
    }
  }
  text = json_dumps(arr, JSON_COMPACT);
  json_decref(arr);
  return text;
}
