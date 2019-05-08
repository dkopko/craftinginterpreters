#ifndef clox_table_h
#define clox_table_h

#include "cb_integration.h"
#include "common.h"
#include "value.h"

typedef struct {
  cb_offset_t root;
} Table;

void initTable(Table                *table,
               cb_term_comparator_t  term_cmp,
               cb_term_render_t      term_render);

void freeTable(Table *table);

bool tableGet(Table *table, Value key, Value *value);

bool tableSet(Table *table, Value key, Value value);

bool tableDelete(Table *table, Value key);

void tableAddAll(Table *from, Table *to);

CBO<ObjString> tableFindString(Table      *table,
                               const char *chars,
                               int         length,
                               uint32_t    hash);

void tableRemoveWhite(Table* table);

void grayTable(Table* table);

#endif
