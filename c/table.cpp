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
                    term_cmp,
                    term_render,
                    term_render,
                    &clox_no_external_size,
                    &clox_no_external_size);
  assert(ret == 0);

  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(table->root_b),
                    term_cmp,
                    term_cmp,
                    term_render,
                    term_render,
                    &clox_no_external_size,
                    &clox_no_external_size);
  assert(ret == 0);

  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(table->root_c),
                    term_cmp,
                    term_cmp,
                    term_render,
                    term_render,
                    &clox_no_external_size,
                    &clox_no_external_size);
  assert(ret == 0);

}

void
freeTable(Table* table)
{
  //CBINT Redundant
  (void)table;
}

bool
tableGet(const Table *table,
         Value        key,
         Value       *value)
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
  const Table *src;
  Table       *dest;
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
tableAddAll(const Table *from,
            Table       *to)
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
                cb_offset_t offset,
                const char *chars,
                int         length,
                uint32_t    hash)
{
  if (offset == CB_NULL) {
    printf("DANDEBUG %p tableFindString(%.*s@RAW)\n", table, length, chars);
  } else {
    printf("DANDEBUG %p tableFindString(%.*s@%ju)\n", table, length, chars, (uintmax_t)offset);
  }
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
    printf("DANDEBUG %p tableFindString string#%ju@%ju\"%s\" -> NOT FOUND\n",
           table,
           (uintmax_t)lookupStringOID.id().id,
           (uintmax_t)lookupStringOID.clip()->chars.co(),
           lookupStringOID.clip()->chars.clp());
    return CB_NULL_OID;
  }

  internedStringValue = numToValue(cb_term_get_dbl(&value_term));
  printf("DANDEBUG %p tableFindString string#%ju@%ju\"%s\" -> string#%ju@%ju\"%s\"\n",
      table,
      (uintmax_t)lookupStringOID.id().id,
      (uintmax_t)lookupStringOID.clip()->chars.co(),
      lookupStringOID.clip()->chars.clp(),
      (uintmax_t)AS_OBJ_ID(internedStringValue).id,
      (uintmax_t)((ObjString*)AS_OBJ(internedStringValue))->chars.co(),
      ((ObjString*)AS_OBJ(internedStringValue))->chars.clp());

  return AS_OBJ_ID(internedStringValue);
}

struct delete_white_keys_closure
{
  struct cb        **cb;
  struct cb_region  *region;
  cb_offset_t        new_root;
  cb_offset_t        cutoff_offset;
};

static int
delete_white_keys(const struct cb_term *key_term,
                  const struct cb_term *value_term,
                  void                 *closure)
{
  struct delete_white_keys_closure *c = (struct delete_white_keys_closure *)closure;
  Value keyValue = numToValue(cb_term_get_dbl(key_term));
  int ret;

  if (isWhite(keyValue)) {
    printf("DANDEBUG deleting white key of ");
    printObjectValue(keyValue);
    printf("\n");

    ret = cb_bst_delete(c->cb,
                        c->region,
                        &(c->new_root),
                        c->cutoff_offset,
                        key_term);
    assert(ret == 0);
    (void)ret;
  }

  return CB_SUCCESS;
}

void
tableRemoveWhite(Table *table)
{
  struct delete_white_keys_closure closure = { &thread_cb,
                                               &thread_region,
                                               table->root_a,
                                               cb_cursor(thread_cb) };
  int ret;

  ret = cb_bst_traverse(thread_cb,
                        table->root_a,
                        &delete_white_keys,
                        &closure);
  assert(ret == 0);
  (void)ret;

  table->root_a = closure.new_root;
}

static int
gray_entry(const struct cb_term *key_term,
           const struct cb_term *value_term,
           void                 *closure)
{
  grayValue(numToValue(cb_term_get_dbl(key_term)));
  grayValue(numToValue(cb_term_get_dbl(value_term)));

  return CB_SUCCESS;
}

static int
gray_entry_if_in_b_or_c(const struct cb_term *key_term,
                        const struct cb_term *value_term,
                        void                 *closure)
{
  Table *table = (Table *)closure;
  struct cb_term temp_value_term;
  int ret;

  ret = cb_bst_lookup(thread_cb, table->root_b, key_term, &temp_value_term);
  if (ret == 0) goto do_gray;
  ret = cb_bst_lookup(thread_cb, table->root_c, key_term, &temp_value_term);
  if (ret == 0) goto do_gray;
  goto done;

do_gray:
  grayValue(numToValue(cb_term_get_dbl(key_term)));
  grayValue(numToValue(cb_term_get_dbl(value_term)));

done:
  return CB_SUCCESS;
}

void
grayTable(Table* table)
{
  // Graying the table means that all keys and values of all entries of this
  // table are marked as "still in use". Everything recursively reachable from
  // the keys and values will also be marked.

  //CBINT FIXME Under the new GC paradigm, this could probably be optimized with epochs.

  int ret;

  ret = cb_bst_traverse(thread_cb,
                        table->root_a,
                        &gray_entry,
                        NULL);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        table->root_b,
                        &gray_entry,
                        NULL);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        table->root_c,
                        &gray_entry,
                        NULL);
  assert(ret == 0);

  (void)ret;
}

void
grayInterningTable(Table* table)
{
  //NOTE: This is a special case needed for a table used for interning, written
  //  with a nod to generality (i.e. handling TOMBSTONEs, which will never
  //  truly exist in vm.strings). Using this on vm.strings will probably be the
  //  only case we ever need.
  //
  //  The idea is to mark all "load-bearing" keys.  For table A, this is any key
  //  which masks an entry in B or C, whether or not this key contains a value
  //  or a TOMBSTONE. For those keys in A which do not mask entries in B or C,
  //  then we require reachability from elsewhere in the program state to gray
  //  this entry, which signifies it is still in use aside from our own use of
  //  it.  For tables B and C, these tables must remain static in their
  //  contents, and we must not allow any key or value still existing in the
  //  table to become invalid, so all keys and values of these tables get
  //  marked.
  //
  // For every entry in A, gray key and value if key is present in B or C.
  //  (This is needed even if A's entry's value is just a TOMBSTONE which masks
  //   an entry in B and/or C.)
  // For every entry in B, gray key and value.
  // For every entry in C, gray key and value.

  int ret;

  ret = cb_bst_traverse(thread_cb,
                        table->root_a,
                        &gray_entry_if_in_b_or_c,
                        table);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        table->root_b,
                        &gray_entry,
                        NULL);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        table->root_c,
                        &gray_entry,
                        NULL);
  assert(ret == 0);

  (void)ret;
}

struct printTableClosure
{
  const char *desc0;
  const char *desc1;
};

static int
printTableTraversal(const struct cb_term *key_term,
                    const struct cb_term *value_term,
                    void                 *closure)
{
  struct printTableClosure *clo = (struct printTableClosure *)closure;
  Value keyValue = numToValue(cb_term_get_dbl(key_term));
  Value valueValue = numToValue(cb_term_get_dbl(value_term));

  printf("%s %s ", clo->desc0, clo->desc1);
  printValue(keyValue);
  printf(" -> ");
  printValue(valueValue);
  printf("\n");

  return 0;
}


void
printTable(Table* table, const char *desc)
{
  struct printTableClosure closure;
  int ret;

  closure.desc0 = desc;

  closure.desc1 = "A";
  ret = cb_bst_traverse(thread_cb,
                        table->root_a,
                        &printTableTraversal,
                        &closure);
  assert(ret == 0);

  closure.desc1 = "B";
  ret = cb_bst_traverse(thread_cb,
                        table->root_b,
                        &printTableTraversal,
                        &closure);
  assert(ret == 0);

  closure.desc1 = "C";
  ret = cb_bst_traverse(thread_cb,
                        table->root_c,
                        &printTableTraversal,
                        &closure);
  assert(ret == 0);

  (void)ret;
}

