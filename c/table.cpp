#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "dandebug.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "cb_bst.h"

void
initTable(Table                *table,
          cb_term_comparator_t  term_cmp,
          cb_term_render_t      term_render)
{
  int ret;
  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(table->root),
                    term_cmp,
                    term_render);
  assert(ret == 0);
  (void)ret;
}

void
freeTable(Table* table)
{
  initTable(table,
            cb_bst_cmp_get(thread_cb, table->root),
            cb_bst_render_get(thread_cb, table->root));
}

bool
tableGet(Table *table,
         Value  key,
         Value *value)
{
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  DANDEBUG("Looking up ");
  printValue(key);

  cb_term_set_dbl(&key_term, valueToNum(key));

  ret = cb_bst_lookup(thread_cb, table->root, &key_term, &value_term);
  if (ret != 0) {
    printf("DANDEBUG %p tableGet ", &(table->root));
    printValue(key);
    printf(" -> NOT FOUND\n");
    return false;
  }

  *value = numToValue(cb_term_get_dbl(&value_term));
  printf("DANDEBUG %p tableGet ", &(table->root));
  printValue(key);
  printf(" -> ");
  printValue(*value);
  printf("\n");
  return true;
}

bool
tableSet(Table* table, Value key, Value value)
{
  struct cb_term key_term;
  struct cb_term value_term;
  int lookup_ret, ret;
  bool lookup_ret2;

  cb_term_set_dbl(&key_term, valueToNum(key));
  cb_term_set_dbl(&value_term, valueToNum(value));

  printf("DANDEBUG %p(o:%ju) about to tableSet ", &(table->root), (uintmax_t)table->root);
  printValue(key);
  printf(" -> ");
  printValue(value);
  printf(", Table looks like:\n");
  cb_bst_print(&thread_cb, table->root);
  printf("(endtable)\n");


  lookup_ret = cb_bst_contains_key(thread_cb, table->root, &key_term);
  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      &(table->root),
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  assert(ret == 0);

  printf("DANDEBUG %p(o:%ju) new table is: \n", &(table->root), (uintmax_t)table->root);
  cb_bst_print(&thread_cb, table->root);
  printf("(endtable)\n");

  printf("About to do cb_bst_contains_key()\n");
  lookup_ret2 = cb_bst_contains_key(thread_cb, table->root, &key_term);
  (void)lookup_ret2;
  printf("Completed cb_bst_contains_key(), ret: %s\n", (lookup_ret2 ? "true" : "false"));

  printf("DANDEBUG %p end tableSet ", &(table->root));
  printValue(key);
  printf(" -> ");
  printValue(value);
  printf("\n");

  assert(lookup_ret2 == true);

  //true if new key and we succeeded at inserting it.
  return (!lookup_ret && ret == 0);
}

bool
tableDelete(Table *table,
            Value  key)
{
  struct cb_term key_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));

  ret = cb_bst_delete(&thread_cb,
                      &thread_region,
                      &(table->root),
                      thread_cutoff_offset,
                      &key_term);
  return (ret == 0);
}

static int
traversalAdd(const struct cb_term *key_term,
             const struct cb_term *value_term,
             void                 *closure)
{
  Table *dest = (Table *)closure;

  tableSet(dest,
           numToValue(cb_term_get_dbl(key_term)),
           numToValue(cb_term_get_dbl(value_term)));

  return 0;
}

void
tableAddAll(Table *from,
            Table* to)
{
  int ret;

  ret = cb_bst_traverse(thread_cb,
                        from->root,
                        &traversalAdd,
                        to);
  assert(ret == 0);
  (void)ret;
}

CBO<ObjString>
tableFindString(Table      *table,
                const char *chars,
                int         length,
                uint32_t    hash)
{
  printf("DANDEBUG tableFindString(%.*s)\n", length, chars);
  //FIXME CBINT rewind if lookup fails?
  CBO<ObjString> lookupStringCBO = rawAllocateString(chars, length);
  Value lookupStringValue = OBJ_VAL(lookupStringCBO.o());
  Value internedStringValue;
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(lookupStringValue));

  ret = cb_bst_lookup(thread_cb, table->root, &key_term, &value_term);
  if (ret != 0) {
    printf("DANDEBUG %p tableFindString string@%ju\"%s\"(%ju) -> NOT FOUND\n",
           &(table->root),
           (uintmax_t)lookupStringCBO.o(),
           lookupStringCBO.lp()->chars.lp(),
           lookupStringCBO.lp()->chars.o());
    return CB_NULL;
  }

  internedStringValue = numToValue(cb_term_get_dbl(&value_term));
  printf("DANDEBUG %p tableFindString string@%ju\"%s\"(%ju) -> string@%ju\"%s\"(%ju)\n",
      &(table->root),
      (uintmax_t)lookupStringCBO.o(),
      lookupStringCBO.lp()->chars.lp(),
      (uintmax_t)lookupStringCBO.lp()->chars.o(),
      (uintmax_t)AS_OBJ_OFFSET(internedStringValue),
      ((ObjString*)AS_OBJ(internedStringValue))->chars.lp(),
      (uintmax_t)((ObjString*)AS_OBJ(internedStringValue))->chars.o());

  return AS_OBJ_OFFSET(internedStringValue);
}

void
tableRemoveWhite(Table *table)
{
  //CBINT Redundant
}

void grayTable(Table* table)
{
  //CBINT Redundant
}
