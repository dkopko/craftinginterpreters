//> Hash Tables table-c
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "dandebug.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "cb_bst.h"

//> max-load
#define TABLE_MAX_LOAD 0.75

//< max-load
void initTable(Table* table, cb_term_comparator_t term_cmp) {
  int ret;
  table->count = 0;
/* Hash Tables table-c < Optimization not-yet
  table->capacity = 0;
*/
//> Optimization not-yet
  table->capacityMask = -1;
//< Optimization not-yet
  table->entries = CB_NULL;

  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(table->root),
                    term_cmp);
  assert(ret == 0);
  (void)ret;
}
//> free-table
void freeTable(Table* table) {
/* Hash Tables free-table < Optimization not-yet
  FREE_ARRAY(Entry, table->entries, table->capacity);
*/
//> Optimization not-yet
  FREE_ARRAY(Entry, table->entries, table->capacityMask + 1);
//< Optimization not-yet
  initTable(table, cb_bst_cmp_get(thread_cb, table->root));
}
//< free-table
//> find-entry
/* Hash Tables find-entry < Optimization not-yet
static Entry* findEntry(Entry* entries, int capacity,
                        ObjString* key) {
*/
//> Optimization not-yet
static Entry* findEntry(CBO<Entry> entries, int capacityMask,
                        CBO<ObjString> key) {
//< Optimization not-yet
/* Hash Tables find-entry < Optimization not-yet
  uint32_t index = key->hash % capacity;
*/
//> Optimization not-yet
  uint32_t index = key.lp()->hash & capacityMask;
//< Optimization not-yet
//> find-entry-tombstone
  Entry* tombstone = NULL;
  
//< find-entry-tombstone
  for (;;) {
    Entry* entry = &entries.lp()[index];

/* Hash Tables find-entry < Hash Tables find-tombstone
    if (entry->key == key || entry->key == NULL) {
      return entry;
    }
*/
//> find-tombstone
    if (entry->key.is_nil()) {
      if (IS_NIL(entry->value)) {
        // Empty entry.
        return tombstone != NULL ? tombstone : entry;
      } else {
        // We found a tombstone.
        if (tombstone == NULL) tombstone = entry;
      }
    } else if (entry->key.o() == key.o()) {
      // We found the key.
      return entry;
    }
//< find-tombstone

/* Hash Tables find-entry < Optimization not-yet
    index = (index + 1) % capacity;
*/
//> Optimization not-yet
    index = (index + 1) & capacityMask;
//< Optimization not-yet
  }
}
//< find-entry
//> table-get
bool tableGet(Table* table, Value key, Value* value) {
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

#if 0
  if (table->entries.is_nil()) return false;

/* Hash Tables table-get < Optimization not-yet
  Entry* entry = findEntry(table->entries, table->capacity, key);
*/
//> Optimization not-yet
  Entry* entry = findEntry(table->entries, table->capacityMask, key);
//< Optimization not-yet
  if (entry->key.is_nil()) return false;

  *value = entry->value;
  return true;
#endif
}
//< table-get
//> table-adjust-capacity
/* Hash Tables table-adjust-capacity < Optimization not-yet
static void adjustCapacity(Table* table, int capacity) {
*/
//> Optimization not-yet
static void adjustCapacity(Table* table, int capacityMask) {
//< Optimization not-yet
/* Hash Tables table-adjust-capacity < Optimization not-yet
  Entry* entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
*/
//> Optimization not-yet
  CBO<Entry> entriesCBO = ALLOCATE(Entry, capacityMask + 1);
  Entry* entries = entriesCBO.lp();
  for (int i = 0; i <= capacityMask; i++) {
//< Optimization not-yet
    entries[i].key = CB_NULL;
    entries[i].value = NIL_VAL;
  }
  
//> re-hash
//> resize-init-count
  table->count = 0;
//< resize-init-count
/* Hash Tables re-hash < Optimization not-yet
  for (int i = 0; i < table->capacity; i++) {
*/
//> Optimization not-yet
  for (int i = 0; i <= table->capacityMask; i++) {
//< Optimization not-yet
    Entry* entry = &table->entries.lp()[i];
    if (entry->key.is_nil()) continue;

/* Hash Tables re-hash < Optimization not-yet
    Entry* dest = findEntry(entries, capacity, entry->key);
*/
//> Optimization not-yet
    Entry* dest = findEntry(entriesCBO, capacityMask, entry->key);
//< Optimization not-yet
    dest->key = entry->key;
    dest->value = entry->value;
//> resize-increment-count
    table->count++;
//< resize-increment-count
  }
//< re-hash

/* Hash Tables free-old-array < Optimization not-yet
  FREE_ARRAY(Entry, table->entries, table->capacity);
*/
//> Optimization not-yet
  FREE_ARRAY(Entry, table->entries, table->capacityMask + 1);
//< Optimization not-yet
  table->entries = entriesCBO;
/* Hash Tables table-adjust-capacity < Optimization not-yet
  table->capacity = capacity;
*/
//> Optimization not-yet
  table->capacityMask = capacityMask;
//< Optimization not-yet
}
//< table-adjust-capacity
//> table-set
bool tableSet(Table* table, Value key, Value value) {
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

  printf("DANDEBUG %p(o:%ju) new table is: ", &(table->root), (uintmax_t)table->root);
  cb_bst_print(&thread_cb, table->root);
  printf("(endtable)\n");

  printf("About to do cb_bst_contains_key()\n");
  lookup_ret2 = cb_bst_contains_key(thread_cb, table->root, &key_term);
  (void)lookup_ret2;
  printf("Completed cb_bst_contains_key(), ret: %d\n", lookup_ret2);

  printf("DANDEBUG %p end tableSet ", &(table->root));
  printValue(key);
  printf(" -> ");
  printValue(value);
  printf("\n");

  assert(lookup_ret2 == true);

  //true if new key and we succeeded at inserting it.
  return (!lookup_ret && ret == 0);

#if 0
/* Hash Tables table-set-grow < Optimization not-yet
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
*/
//> Optimization not-yet
  if (table->count + 1 > (table->capacityMask + 1) * TABLE_MAX_LOAD) {
    // Figure out the new table size.
    int capacityMask = GROW_CAPACITY(table->capacityMask + 1) - 1;
    adjustCapacity(table, capacityMask);
//< Optimization not-yet
//> table-set-grow
  }

//< table-set-grow
/* Hash Tables table-set < Optimization not-yet
  Entry* entry = findEntry(table->entries, table->capacity, key);
*/
//> Optimization not-yet
  Entry* entry = findEntry(table->entries, table->capacityMask, key);
//< Optimization not-yet
  
  bool isNewKey = entry->key.is_nil();
/* Hash Tables table-set < Hash Tables set-increment-count
  if (isNewKey) table->count++;
*/
//> set-increment-count
  if (isNewKey && IS_NIL(entry->value)) table->count++;
//< set-increment-count

  entry->key = key;
  entry->value = value;
  return isNewKey;
#endif
}
//< table-set
//> table-delete
bool tableDelete(Table* table, Value key) {
  struct cb_term key_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));

  ret = cb_bst_delete(&thread_cb,
                      &thread_region,
                      &(table->root),
                      thread_cutoff_offset,
                      &key_term);
  return (ret == 0);

#if 0
  if (table->count == 0) return false;

  // Find the entry.
/* Hash Tables table-delete < Optimization not-yet
  Entry* entry = findEntry(table->entries, table->capacity, key);
*/
//> Optimization not-yet
  Entry* entry = findEntry(table->entries, table->capacityMask, key);
//< Optimization not-yet
  if (entry->key.is_nil()) return false;

  // Place a tombstone in the entry.
  entry->key = CB_NULL;
  entry->value = BOOL_VAL(true);

  return true;
#endif
}
//< table-delete
//> table-add-all
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

void tableAddAll(Table* from, Table* to) {
  int ret;

  ret = cb_bst_traverse(thread_cb,
                        from->root,
                        &traversalAdd,
                        to);
  assert(ret == 0);
#if 0
/* Hash Tables table-add-all < Optimization not-yet
  for (int i = 0; i < from->capacity; i++) {
*/
//> Optimization not-yet
  for (int i = 0; i <= from->capacityMask; i++) {
//< Optimization not-yet
    Entry* entry = &from->entries.lp()[i];
    if (!entry->key.is_nil()) {
      tableSet(to, OBJ_VAL(entry->key.o()), entry->value);
    }
  }
#endif
}
//< table-add-all
//> table-find-string
CBO<ObjString> tableFindString(Table* table, const char* chars, int length,
                               uint32_t hash) {
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
    printf("DANDEBUG %p tableFindString \"", &(table->root));
    printValue(lookupStringValue);
    printf("\" -> NOT FOUND\n");
    return CB_NULL;
  }

  internedStringValue = numToValue(cb_term_get_dbl(&value_term));
  printf("DANDEBUG %p tableFindString \"", &(table->root));
  printValue(lookupStringValue);
  printf("\" (%ju) -> \"", (uintmax_t)lookupStringCBO.o());
  printValue(internedStringValue);
  printf("\" (%ju)\n", (uintmax_t)AS_OBJ_OFFSET(internedStringValue));
  return AS_OBJ_OFFSET(internedStringValue);

#if 0
  // If the table is empty, we definitely won't find it.
  if (table->entries.is_nil()) return CB_NULL;

/* Hash Tables table-find-string < Optimization not-yet
  uint32_t index = hash % table->capacity;
*/
//> Optimization not-yet
  uint32_t index = hash & table->capacityMask;
//< Optimization not-yet

  for (;;) {
    Entry* entry = &table->entries.lp()[index];

    if (entry->key.is_nil()) {
      // Stop if we find an empty non-tombstone entry.
      if (IS_NIL(entry->value)) return CB_NULL;
    } else if (entry->key.lp()->length == length &&
        entry->key.lp()->hash == hash &&
        memcmp(entry->key.lp()->chars.lp(), chars, length) == 0) {
      // We found it.
      return entry->key;
    }

    // Try the next slot.
/* Hash Tables table-find-string < Optimization not-yet
    index = (index + 1) % table->capacity;
*/
//> Optimization not-yet
    index = (index + 1) & table->capacityMask;
//< Optimization not-yet
  }
#endif
}
//< table-find-string
//> Garbage Collection not-yet

void tableRemoveWhite(Table* table) {
  return; //CBINT
/* Garbage Collection not-yet < Optimization not-yet
  for (int i = 0; i < table->capacity; i++) {
*/
//> Optimization not-yet
  for (int i = 0; i <= table->capacityMask; i++) {
//< Optimization not-yet
    Entry* entry = &table->entries.lp()[i];
    if (!entry->key.is_nil() && !entry->key.lp()->obj.isDark) {
      tableDelete(table, OBJ_VAL(entry->key.o()));
    }
  }
}

void grayTable(Table* table) {
  return; //CBINT
/* Garbage Collection not-yet < Optimization not-yet
  for (int i = 0; i < table->capacity; i++) {
*/
//> Optimization not-yet
  for (int i = 0; i <= table->capacityMask; i++) {
//< Optimization not-yet
    Entry* entry = &table->entries.lp()[i];
    grayObject((Obj*)entry->key.lp());
    grayValue(entry->value);
  }
}
//< Garbage Collection not-yet
