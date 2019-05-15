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

  (void)ret;

  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(table->root_a),
                    term_cmp,
                    term_render,
                    &clox_value_external_size);
  assert(ret == 0);

  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(table->root_b),
                    term_cmp,
                    term_render,
                    &clox_value_external_size);
  assert(ret == 0);

  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(table->root_c),
                    term_cmp,
                    term_render,
                    &clox_value_external_size);
  assert(ret == 0);

}

void
freeTable(Table* table)
{
  //CBINT Redundant
  (void)table;
}

bool
tableGet(Table *table,
         Value  key,
         Value *value)
{
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));

  ret = cb_bst_lookup(thread_cb, table->root_a, &key_term, &value_term);
  if (ret == 0) goto done;
  ret = cb_bst_lookup(thread_cb, table->root_b, &key_term, &value_term);
  if (ret == 0) goto done;
  ret = cb_bst_lookup(thread_cb, table->root_c, &key_term, &value_term);

done:
  if (ret != 0 || numToValue(cb_term_get_dbl(&value_term)).val == TOMBSTONE_VAL.val) {
    return false;
  }

  *value = numToValue(cb_term_get_dbl(&value_term));
  return true;
}

bool
tableSet(Table* table, Value key, Value value)
{
  struct cb_term key_term;
  struct cb_term value_term;
  Value temp_value;
  bool already_exists;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));
  cb_term_set_dbl(&value_term, valueToNum(value));

  //CBINT FIXME would be nice to avoid this lookup by leveraging
  // cb_bst_insert()'s lookup.
  already_exists = tableGet(table, key, &temp_value);

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      &(table->root_a),
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  assert(ret == 0);

  //true if new key and we succeeded at inserting it.
  return (!already_exists && ret == 0);
}

bool
tableDelete(Table *table,
            Value  key)
{
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));
  cb_term_set_dbl(&value_term, valueToNum(TOMBSTONE_VAL));

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      &(table->root_a),
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  return (ret == 0);
}

struct TraversalAddClosure
{
  Table *src;
  Table *dest;
};

static int
traversalAdd(const struct cb_term *key_term,
             const struct cb_term *value_term,
             void                 *closure)
{
  TraversalAddClosure *taclosure = (TraversalAddClosure *)closure;

  Value key = numToValue(cb_term_get_dbl(key_term));
  Value value = numToValue(cb_term_get_dbl(value_term));
  Value tempValue;

  //CBINT FIXME: This need not check all BSTs in the tritable for all
  //  traversals, but I don't want to write the specializations now.
  //need to traverse C, inserting those not tombstone in A or B or C (C can't happen).
  //need to traverse B, inserting those not tombstone in A or B.
  //need to traverse A, inserting those not tombstone in A.
  if (tableGet(taclosure->src, key, &tempValue)) {
    tableSet(taclosure->dest, key, value);
  }

  return 0;
}

void
tableAddAll(Table *from,
            Table* to)
{
  TraversalAddClosure taclosure = { from, to };
  int ret;

  ret = cb_bst_traverse(thread_cb,
                        from->root_c,
                        &traversalAdd,
                        &taclosure);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        from->root_b,
                        &traversalAdd,
                        &taclosure);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        from->root_a,
                        &traversalAdd,
                        &taclosure);
  assert(ret == 0);
  (void)ret;
}

OID<ObjString>
tableFindString(Table      *table,
                const char *chars,
                int         length,
                uint32_t    hash)
{
  printf("DANDEBUG %p tableFindString(%.*s)\n", table, length, chars);
  //FIXME CBINT rewind if lookup fails?
  OID<ObjString> lookupStringOID = rawAllocateString(chars, length);
  Value lookupStringValue = OBJ_VAL(lookupStringOID.id());
  Value internedStringValue;
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(lookupStringValue));

  ret = cb_bst_lookup(thread_cb, table->root_a, &key_term, &value_term);
  if (ret == 0) goto done;
  ret = cb_bst_lookup(thread_cb, table->root_b, &key_term, &value_term);
  if (ret == 0) goto done;
  ret = cb_bst_lookup(thread_cb, table->root_c, &key_term, &value_term);

done:
  if (ret != 0 || numToValue(cb_term_get_dbl(&value_term)).val == TOMBSTONE_VAL.val) {
    printf("DANDEBUG %p tableFindString string@%ju\"%s\"(%ju) -> NOT FOUND\n",
           table,
           (uintmax_t)lookupStringOID.id().id,
           lookupStringOID.lp()->chars.lp(),
           lookupStringOID.lp()->chars.id().id);
    return CB_NULL_OID;
  }

  internedStringValue = numToValue(cb_term_get_dbl(&value_term));
  printf("DANDEBUG %p tableFindString string@%ju\"%s\"(%ju) -> string@%ju\"%s\"(%ju)\n",
      table,
      (uintmax_t)lookupStringOID.id().id,
      lookupStringOID.lp()->chars.lp(),
      (uintmax_t)lookupStringOID.lp()->chars.id().id,
      (uintmax_t)AS_OBJ_ID(internedStringValue).id,
      ((ObjString*)AS_OBJ(internedStringValue))->chars.lp(),
      (uintmax_t)((ObjString*)AS_OBJ(internedStringValue))->chars.id().id);

  return AS_OBJ_ID(internedStringValue);
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
