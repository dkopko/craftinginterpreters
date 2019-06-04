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
    printf("DANDEBUG %p tableFindString string#%ju\"%s\"#%ju -> NOT FOUND\n",
           table,
           (uintmax_t)lookupStringOID.id().id,
           lookupStringOID.lp()->chars.lp(),
           lookupStringOID.lp()->chars.id().id);
    return CB_NULL_OID;
  }

  internedStringValue = numToValue(cb_term_get_dbl(&value_term));
  printf("DANDEBUG %p tableFindString string#%ju\"%s\"#%ju -> string#%ju\"%s\"#%ju\n",
      table,
      (uintmax_t)lookupStringOID.id().id,
      lookupStringOID.lp()->chars.lp(),
      (uintmax_t)lookupStringOID.lp()->chars.id().id,
      (uintmax_t)AS_OBJ_ID(internedStringValue).id,
      ((ObjString*)AS_OBJ(internedStringValue))->chars.lp(),
      (uintmax_t)((ObjString*)AS_OBJ(internedStringValue))->chars.id().id);

  return AS_OBJ_ID(internedStringValue);
}

//CBINT FIXME Redundant
void
tableRemoveWhite(Table *table)  //really removeStringsTableWhiteKeys()
{
  // There are several issues going on here:
  // 1) This function was only ever used on vm.strings, but was written
  //    generally.  Should we maintain the general case in the rewrite?
  // 2) Table deletions were true deletions, whereas tri-table deletions are
  //    the writing of TOMBSTONEs under the given key (to mask older values in
  //    the earlier parts of the tri-table).  Therefore, deleted keys cannot
  //    truly be collected (though their values can be).
  // 3) Strings are no longer being exclusively interned.  It used to be the
  //    case that the table was queried directly when interning a given string
  //    described by a char *.  Now, a temporary ObjString key is generated to
  //    query the tri-table for the given key when interning, which means that
  //    for a little while there will be two ObjStrings with the same contents.
  //    Such temporary ObjStrings may ultimately occur at other times as well.
  // 4) In the old case, removal of white keys of vm.strings means that these
  //    strings occur no where in the rest of the state of the VM (or else they
  //    would have been visited and marked).  If we presume that any new
  //    occurence of this string would force a new entry into the A table of
  //    vm.strings, can we get away with just deleting from the A table and
  //    disregarding any presence in B or C?  Unfortunately, no, as the B and
  //    C tables need to still be queried at times, and if an entry in these
  //    tables' structures exists, but the key's memory has been reused, then
  //    this cannot work.  It would be fine to truly delete from A, but B and
  //    C cannot be considered modifiable.  This means, in fact, that presence
  //    of a key in in the B or C table of vm.strings must entail that it gets
  //    marked as still needed. (Which brings up a side point, which is that
  //    marking the key Obj itself is a mutation at present, but needs to become
  //    some external notion of marking.)

  // Solution:
  // 1) Mark all key Objs in vm.strings sub-tables B and C.
  // 2) Truly delete (don't write TOMBSTONEs) any keys still existing as white
  //    in sub-table A of vm.strings.

  //The idea is that any entry whose key is white must necessarily have no users
  //(or else the key would have been visited and marked).  This can only be
  //true for ObjStrings if they are interned (as they were in the original code,
  //but are no longer).

  //NOTE!! This was only ever used on the vm.strings table in the original
  // code, meaning that interned strings which were not reached by the garbage
  // collector during its recursive grayObject()ing would then be automatically
  // freed.
  //
  // The issue with the new tri-table is that when a key gets "removed' from
  // the tri-table, it is only in a logical sense in that a TOMBSTONE value
  // gets written to the A subtable indicating "this key does not exist in
  // this table" and masking off any values which may have otherwise been
  // found in the deeper B and C layers.  This means that removing a key must
  // still keep the key physically around, at least until such time as it
  // is dropped in generation of a C table.
  //
  // So really, what we have to do is...
  //
  // 1) Mark things that are still doing their job
  // 1a) Mark every TOMBSTONE'd key in A which exists as a non-TOMBSTONE in B or C.
  // 1b) Mark every TOMBSTONE'd key in B which exists as a non-TOMBSTONE in C.
  //
  // 2) Be careful that strings are longer being interned (and so can exist with
  // the same contents in separate locations)
  //    FIXME how to do this?
  //
  //   Mark every key in B which is not in A (including TOMBSTONE'd keys).
  //   Mark every key in C which is not in A or B, unless it's a TOMBSTONE key.
  //   For every white key in B which exists in A, we can truly delete it (not just TOMBSTONE it) from B.
  //   For every key in C which exists in A or B, we can truly delete it (not just TOMBSTONE it) from B.

#if 0
  for (int i = 0; i <= table->capacityMask; i++) {
    Entry* entry = &table->entries[i];
    if (entry->key != NULL && !entry->key->obj.isDark) {
      tableDelete(table, entry->key);
    }
  }
#endif




}

static int
gray_a(const struct cb_term *key_term,
       const struct cb_term *value_term,
       void                 *closure)
{
  grayValue(numToValue(cb_term_get_dbl(key_term)));
  grayValue(numToValue(cb_term_get_dbl(value_term)));

  return CB_SUCCESS;
}

static int
gray_b_not_in_a(const struct cb_term *key_term,
                const struct cb_term *value_term,
                void                 *closure)
{
  Table *table = (Table *)closure;
  struct cb_term temp_value_term;
  int ret;

  grayValue(numToValue(cb_term_get_dbl(key_term)));

  ret = cb_bst_lookup(thread_cb, table->root_a, key_term, &temp_value_term);
  if (ret == 0) goto done;

  grayValue(numToValue(cb_term_get_dbl(value_term)));

done:
  return CB_SUCCESS;
}

static int
gray_c_not_in_a_or_b(const struct cb_term *key_term,
                     const struct cb_term *value_term,
                     void                 *closure)
{
  Table *table = (Table *)closure;
  struct cb_term temp_value_term;
  int ret;

  grayValue(numToValue(cb_term_get_dbl(key_term)));

  ret = cb_bst_lookup(thread_cb, table->root_a, key_term, &temp_value_term);
  if (ret == 0) goto done;
  ret = cb_bst_lookup(thread_cb, table->root_b, key_term, &temp_value_term);
  if (ret == 0) goto done;

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
  //
  // The original implementation here iterated all slots of the table and
  // grayed all the keys and all the values.  Empty/tombstone slots were handled
  // through early-exits of grayObject() and grayValue().  The old
  // implementation only had ObjString keys.  The new implementation allows
  // any Value to be used for a key, but is still only used in contexts where
  // ObjString keys are the only type of keys.

  //For every entry in A, gray its key and value.
  //For every entry in B, gray its key.  Gray its value unless an equal (but perhaps not same!) key is in A.
  //For every entry in C, gray its key.  Gray its value unless an equal (but perhaps not same!) key is in A or B.
  //CBINT FIXME this probably can be optimized with epochs.

  int ret;

  ret = cb_bst_traverse(thread_cb,
                        table->root_a,
                        &gray_a,
                        table);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        table->root_b,
                        &gray_b_not_in_a,
                        table);
  assert(ret == 0);

  ret = cb_bst_traverse(thread_cb,
                        table->root_c,
                        &gray_c_not_in_a_or_b,
                        table);
  assert(ret == 0);

  (void)ret;
}
