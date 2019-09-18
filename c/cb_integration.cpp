#include "cb_integration.h"
#include "cb_bst.h"

#include <assert.h>
#include <stdio.h>
#include "object.h"
#include "memory.h"
#include "value.h"


__thread struct cb        *thread_cb            = NULL;
__thread struct cb_region  thread_region;
__thread cb_offset_t       thread_cutoff_offset = (cb_offset_t)0ULL;
__thread struct ObjTable   thread_objtable;
__thread cb_offset_t       thread_darkset_bst   = CB_BST_SENTINEL;

void
objtable_init(ObjTable *obj_table)
{
  obj_table->root_a = CB_BST_SENTINEL;
  obj_table->root_b = CB_BST_SENTINEL;
  obj_table->root_c = CB_BST_SENTINEL;
  obj_table->next_obj_id.id  = 1;
}

void
objtable_add_at(ObjTable *obj_table, ObjID obj_id, cb_offset_t offset)
{
  cb_term key_term;
  cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, obj_id.id);
  cb_term_set_u64(&value_term, offset);

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      &(obj_table->root_a),
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  assert(ret == 0);
  (void)ret;
}

ObjID
objtable_add(ObjTable *obj_table, cb_offset_t offset)
{
  ObjID obj_id = obj_table->next_obj_id;

  objtable_add_at(obj_table, obj_id, offset);
  (obj_table->next_obj_id.id)++;

  return obj_id;
}

cb_offset_t
objtable_lookup(ObjTable *obj_table, ObjID obj_id)
{
  cb_term key_term;
  cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, obj_id.id);

  ret = cb_bst_lookup(thread_cb, obj_table->root_a, &key_term, &value_term);
  if (ret == 0) goto done;
  ret = cb_bst_lookup(thread_cb, obj_table->root_b, &key_term, &value_term);
  if (ret == 0) goto done;
  ret = cb_bst_lookup(thread_cb, obj_table->root_c, &key_term, &value_term);
  if (ret == 0) goto done;

done:
  if (ret != CB_SUCCESS || cb_term_get_u64(&value_term) == CB_NULL) {
    return CB_NULL;
  }

  return (cb_offset_t)cb_term_get_u64(&value_term);
}

cb_offset_t
objtable_lookup_A(ObjTable *obj_table, ObjID obj_id)
{
  cb_term key_term;
  cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, obj_id.id);

  ret = cb_bst_lookup(thread_cb, obj_table->root_a, &key_term, &value_term);
  if (ret != CB_SUCCESS || cb_term_get_u64(&value_term) == CB_NULL) {
    return CB_NULL;
  }

  return (cb_offset_t)cb_term_get_u64(&value_term);
}

cb_offset_t
objtable_lookup_B(ObjTable *obj_table, ObjID obj_id)
{
  cb_term key_term;
  cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, obj_id.id);

  ret = cb_bst_lookup(thread_cb, obj_table->root_b, &key_term, &value_term);
  if (ret != CB_SUCCESS || cb_term_get_u64(&value_term) == CB_NULL) {
    return CB_NULL;
  }

  return (cb_offset_t)cb_term_get_u64(&value_term);
}

cb_offset_t
objtable_lookup_C(ObjTable *obj_table, ObjID obj_id)
{
  cb_term key_term;
  cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, obj_id.id);

  ret = cb_bst_lookup(thread_cb, obj_table->root_c, &key_term, &value_term);
  if (ret != CB_SUCCESS || cb_term_get_u64(&value_term) == CB_NULL) {
    return CB_NULL;
  }

  return (cb_offset_t)cb_term_get_u64(&value_term);
}

void
objtable_invalidate(ObjTable *obj_table, ObjID obj_id)
{
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, (uint64_t)obj_id.id);
  cb_term_set_u64(&value_term, (uint64_t)CB_NULL);

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      &(obj_table->root_a),
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  assert(ret == CB_SUCCESS);
  (void)ret;
}

cb_offset_t
resolveAsMutable(ObjID objid)
{
  cb_offset_t o;

  o = objtable_lookup_A(&thread_objtable, objid);
  if (o != CB_NULL) {
    //printf("#%ju@%ju found in objtable A\n", (uintmax_t)objid.id, (uintmax_t)o);
    assert(cb_offset_cmp(o, thread_cutoff_offset) > 0);
    return o;
  }

  o = objtable_lookup_B(&thread_objtable, objid);
  if (o != CB_NULL) {
    //printf("#%ju@%ju found in objtable B\n", (uintmax_t)objid.id, (uintmax_t)o);
    cb_offset_t copy_o = mutableCopyObject(objid, o);
    assert(cb_offset_cmp(copy_o, thread_cutoff_offset) > 0);
    objtable_add_at(&thread_objtable, objid, copy_o);
    //printf("#%ju@%ju is new mutable copy in objtable A\n", (uintmax_t)objid_.id, copy_o);
    //printObjectValue(OBJ_VAL(objid));
    //printf(" is new mutable copy in objtable A\n");
    return copy_o;
  }

  o = objtable_lookup_C(&thread_objtable, objid);
  assert(o != CB_NULL);
  //printf("#%ju@%ju found in objtable C\n", (uintmax_t)objid.id, (uintmax_t)o);
  cb_offset_t copy_o = mutableCopyObject(objid, o);
  assert(cb_offset_cmp(copy_o, thread_cutoff_offset) > 0);
  objtable_add_at(&thread_objtable, objid, copy_o);
  //printf("#%ju@%ju is new mutable copy in objtable A\n", (uintmax_t)objid_.id, copy_o);
  //printObjectValue(OBJ_VAL(objid));
  //printf(" is new mutable copy in objtable A\n");
  return copy_o;
}


static int
clox_object_cmp(Value lhs, Value rhs)
{
  ObjType lhsType = OBJ_TYPE(lhs);
  ObjType rhsType = OBJ_TYPE(rhs);

  if (lhsType < rhsType) return -1;
  if (lhsType > rhsType) return 1;

  switch (lhsType) {
    case OBJ_BOUND_METHOD:
    case OBJ_CLASS:
    case OBJ_CLOSURE:
    case OBJ_FUNCTION:
    case OBJ_INSTANCE:
    case OBJ_NATIVE:
    case OBJ_UPVALUE:
      if (lhs.val < rhs.val) return -1;
      if (lhs.val > rhs.val) return 1;
      return 0;

    case OBJ_STRING: {
      ObjString *lhsString = (ObjString *)AS_OBJ(lhs);
      ObjString *rhsString = (ObjString *)AS_OBJ(rhs);
      int shorterLength = lhsString->length < rhsString->length ? lhsString->length : rhsString->length;
      int ret;

      ret = memcmp(lhsString->chars.clp(), rhsString->chars.clp(), shorterLength);
      if (ret < 0) return -1;
      if (ret > 0) return 1;
      if (lhsString->length < rhsString->length) return -1;
      if (lhsString->length > rhsString->length) return 1;
      return 0;
    }

    default:
      assert(lhsType == OBJ_BOUND_METHOD
             || lhsType == OBJ_CLASS
             || lhsType == OBJ_CLOSURE
             || lhsType == OBJ_FUNCTION
             || lhsType == OBJ_INSTANCE
             || lhsType == OBJ_NATIVE
             || lhsType == OBJ_STRING
             || lhsType == OBJ_UPVALUE);
      return 0;
  }
}

//NOTE: This variant is suitable for comparing strings, pre-interning.
int
clox_value_deep_comparator(const struct cb *cb,
                           const struct cb_term *lhs,
                           const struct cb_term *rhs)
{
  // We expect to only use the double value of cb_terms.
  assert(lhs->tag == CB_TERM_DBL);
  assert(rhs->tag == CB_TERM_DBL);

  Value lhs_val = numToValue(cb_term_get_dbl(lhs));
  Value rhs_val = numToValue(cb_term_get_dbl(rhs));

  ValueType lhs_valtype = getValueType(lhs_val);
  ValueType rhs_valtype = getValueType(rhs_val);

  if (lhs_valtype < rhs_valtype) return -1;
  if (lhs_valtype > rhs_valtype) return 1;

  switch (lhs_valtype) {
    case VAL_BOOL:
      return (int)AS_BOOL(lhs_val) - (int)AS_BOOL(rhs_val);

    case VAL_NIL:
      //NOTE: All NILs are equal (of course).
      return 0;

    case VAL_NUMBER: {
      double lhs_num = AS_NUMBER(lhs_val);
      double rhs_num = AS_NUMBER(rhs_val);
      if (lhs_num < rhs_num) return -1;
      if (lhs_num > rhs_num) return 1;
      //NOTE: This comparator should rely on bitwise comparison when the doubles
      // are not less than and not greater than one another, just in case we are
      // in weird NaN territory.
      if (lhs_val.val < rhs_val.val) return -1;
      if (lhs_val.val > rhs_val.val) return 1;
      return 0;
    }

    case VAL_OBJ:
      return clox_object_cmp(lhs_val, rhs_val);

    default:
      assert(lhs_valtype == VAL_BOOL
             || lhs_valtype == VAL_NIL
             || lhs_valtype == VAL_NUMBER
             || lhs_valtype == VAL_OBJ);
      return 0;
  }
}


//NOTE: This variant is suitable for comparing strings, post-interning.
int
clox_value_shallow_comparator(const struct cb *cb,
                              const struct cb_term *lhs,
                              const struct cb_term *rhs)
{
  // We expect to only use the double value of cb_terms.
  assert(lhs->tag == CB_TERM_DBL);
  assert(rhs->tag == CB_TERM_DBL);

  Value lhsValue = numToValue(cb_term_get_dbl(lhs));
  Value rhsValue = numToValue(cb_term_get_dbl(rhs));

#ifndef NDEBUG
  // Check for string interning errors.
  if (OBJ_TYPE(lhsValue) == OBJ_STRING && OBJ_TYPE(rhsValue) == OBJ_STRING) {
      ObjString *lhsString = (ObjString *)AS_OBJ(lhsValue);
      ObjString *rhsString = (ObjString *)AS_OBJ(rhsValue);

      if (lhsString->length == rhsString->length
          && memcmp(lhsString->chars.clp(), rhsString->chars.clp(), lhsString->length) == 0
          && lhsValue.val != rhsValue.val) {
        fprintf(stderr, "String interning error detected! ObjString(%ju, %ju), \"%.*s\"(%ju, %ju)\n",
               (uintmax_t)lhsValue.val,
               (uintmax_t)rhsValue.val,
               lhsString->length,
               lhsString->chars.clp(),
               (uintmax_t)lhsString->chars.co(),
               (uintmax_t)rhsString->chars.co());
        assert(lhsValue.val == rhsValue.val);
      }
  }
#endif //NDEBUG

  if (lhsValue.val < rhsValue.val) return -1;
  if (lhsValue.val > rhsValue.val) return 1;
  return 0;
}

static int
clox_object_render(cb_offset_t           *dest_offset,
                   struct cb            **cb,
                   const struct cb_term  *term,
                   unsigned int           flags)
{
  // We expect to only use the double value of cb_terms.
  assert(term->tag == CB_TERM_DBL);

  // We expect to only be used on object-type Values.
  Value value = numToValue(cb_term_get_dbl(term));
  assert(getValueType(value) == VAL_OBJ);

  ObjType objType = OBJ_TYPE(value);
  switch (objType) {
    case OBJ_BOUND_METHOD:
      return cb_asprintf(dest_offset, cb, "<bound_method@%ju>", (uintmax_t)AS_BOUND_METHOD_OID(value).id().id);
    case OBJ_CLASS:
      return cb_asprintf(dest_offset, cb, "<class@%ju>", (uintmax_t)AS_CLASS_OID(value).id().id);
    case OBJ_CLOSURE:
      return cb_asprintf(dest_offset, cb, "<closure@%ju>", (uintmax_t)AS_CLOSURE_OID(value).id().id);
    case OBJ_FUNCTION:
      return cb_asprintf(dest_offset, cb, "<fun@%ju>", (uintmax_t)AS_FUNCTION_OID(value).id().id);
    case OBJ_INSTANCE:
      return cb_asprintf(dest_offset, cb, "<instance@%ju>", (uintmax_t)AS_INSTANCE_OID(value).id().id);
    case OBJ_NATIVE:
      return cb_asprintf(dest_offset, cb, "<nativefun%p>", (void*)AS_NATIVE(value));
    case OBJ_STRING:{
      const ObjString *str = AS_STRING_OID(value).clip();

      if (str->length < 13) {
        return cb_asprintf(dest_offset, cb, "<string#%ju\"%.*s\"#%ju>",
            (uintmax_t)AS_STRING_OID(value).id().id,
            str->length,
            str->chars.clp(),
            (uintmax_t)str->chars.co());
      } else {
        return cb_asprintf(dest_offset, cb, "<string#%ju\"%.*s...%.*s\"%ju>",
            (uintmax_t)AS_STRING_OID(value).id().id,
            5,
            str->chars.clp(),
            5,
            str->chars.clp() + str->length - 5,
            (uintmax_t)str->chars.co());
      }
    }
    case OBJ_UPVALUE:
      return cb_asprintf(dest_offset, cb, "<upvalue@%ju>", (uintmax_t)AS_UPVALUE_OID(value).id().id);
    default:
      assert(objType == OBJ_BOUND_METHOD
             || objType == OBJ_CLASS
             || objType == OBJ_CLOSURE
             || objType == OBJ_FUNCTION
             || objType == OBJ_INSTANCE
             || objType == OBJ_NATIVE
             || objType == OBJ_STRING
             || objType == OBJ_UPVALUE);
      return 0;
  }
}

int
clox_value_render(cb_offset_t           *dest_offset,
                  struct cb            **cb,
                  const struct cb_term  *term,
                  unsigned int           flags)
{
  // We expect to only use the double value of cb_terms.
  assert(term->tag == CB_TERM_DBL);

  Value value = numToValue(cb_term_get_dbl(term));

  ValueType valType = getValueType(value);
  switch (valType) {
    case VAL_BOOL:
      return cb_asprintf(dest_offset, cb, "<%db>", (int)AS_BOOL(value));

    case VAL_NIL:
      return cb_asprintf(dest_offset, cb, "<nil>");

    case VAL_NUMBER:
      return cb_asprintf(dest_offset, cb, "<%ff>", AS_NUMBER(value));

    case VAL_OBJ:
      return clox_object_render(dest_offset, cb, term, flags);

    default:
      assert(valType == VAL_BOOL
             || valType == VAL_NIL
             || valType == VAL_NUMBER
             || valType == VAL_OBJ);
      return -1;
  }
}

static size_t
clox_object_external_size(const struct cb      *cb,
                          const struct cb_term *term)
{
  // We expect to only use the double value of cb_terms.
  assert(term->tag == CB_TERM_DBL);

  // We expect to only be used on object-type Values.
  Value value = numToValue(cb_term_get_dbl(term));
  assert(getValueType(value) == VAL_OBJ);

  ObjType objType = OBJ_TYPE(value);
  switch (objType) {
    //NOTE: These sizes should include any alignment.  These sizes should only
    // take into account those things which were *allocated* (and as such would
    // be re-allocated for a copy) for the given object, and not the size of
    // any objects which are simply held as references.
    case OBJ_BOUND_METHOD:
      return sizeof(ObjBoundMethod) + cb_alignof(ObjBoundMethod) - 1;
    case OBJ_CLASS: {
      const ObjClass *klass = AS_CLASS_OID(value).clip();
      return sizeof(ObjClass) + cb_alignof(ObjClass) - 1
             + cb_bst_size(thread_cb, klass->methods_bst);
    }
    case OBJ_CLOSURE: {
      const ObjClosure *closure = AS_CLOSURE_OID(value).clip();
      return sizeof(ObjClosure) + cb_alignof(ObjClosure) - 1
             + closure->upvalueCount * sizeof(ObjUpvalue) + cb_alignof(ObjUpvalue) - 1;
    }
    case OBJ_FUNCTION: {
      const ObjFunction *function = AS_FUNCTION_OID(value).clip();
      return sizeof(ObjFunction) + cb_alignof(ObjFunction) - 1
             + sizeof(ObjString) + cb_alignof(ObjString) - 1 + function->name.clip()->length  //name FIXME CBINT not needed if interning strings.
             + function->chunk.capacity * sizeof(uint8_t) + cb_alignof(uint8_t) - 1         //code
             + function->chunk.capacity * sizeof(int) + cb_alignof(int) - 1                 //lines
             + function->chunk.constants.capacity * sizeof(Value) + cb_alignof(Value) - 1;  //constants.values
    }
    case OBJ_INSTANCE: {
      const ObjInstance *instance = AS_INSTANCE_OID(value).clip();
      return sizeof(ObjInstance) + cb_alignof(ObjInstance) - 1
             + cb_bst_size(thread_cb, instance->fields_bst);
    }
    case OBJ_NATIVE:
      return sizeof(ObjNative) + cb_alignof(ObjNative) - 1;
    case OBJ_STRING: {
      const ObjString *str = AS_STRING_OID(value).clip();
      return sizeof(ObjString) + cb_alignof(ObjString) - 1
             + str->length;
    }
    case OBJ_UPVALUE:
      return sizeof(ObjUpvalue) + cb_alignof(ObjUpvalue) - 1;
    default:
      assert(objType == OBJ_BOUND_METHOD
             || objType == OBJ_CLASS
             || objType == OBJ_CLOSURE
             || objType == OBJ_FUNCTION
             || objType == OBJ_INSTANCE
             || objType == OBJ_NATIVE
             || objType == OBJ_STRING
             || objType == OBJ_UPVALUE);
      return 0;
  }
}

size_t
clox_value_external_size(const struct cb      *cb,
                         const struct cb_term *term)
{
  // We expect to only use the double value of cb_terms.
  assert(term->tag == CB_TERM_DBL);

  Value value = numToValue(cb_term_get_dbl(term));

  ValueType valType = getValueType(value);
  switch (valType) {
    case VAL_BOOL:
    case VAL_NIL:
    case VAL_NUMBER:
      return 0;

    case VAL_OBJ:
      return clox_object_external_size(cb, term);

    default:
      assert(valType == VAL_BOOL
             || valType == VAL_NIL
             || valType == VAL_NUMBER
             || valType == VAL_OBJ);
      return -1;
  }
}

void
clox_on_cb_resize(const struct cb *old_cb, struct cb *new_cb)
{
  fprintf(stderr, "~~~~~~~~~~~~RESIZED from %ju to %ju ~~~~~~~~~~~\n",
          (uintmax_t)cb_ring_size(old_cb), (uintmax_t)cb_ring_size(new_cb));
  fprintf(stderr, "~~~~~~~~~~~~UNIMPLEMENTED (SORRY) ~~~~~~~~~~~\n");
  abort();
}


struct gc_state
{
  struct cb   *scratch_cb;
  cb_offset_t  visited_set_root;
  cb_offset_t  to_be_visited_root;
};
static struct gc_state gc_state;

int
gc_init(void)
{
  struct cb_params cb_params = CB_PARAMS_DEFAULT;

  cb_params.ring_size = 8388608 * 4;
  cb_params.mmap_flags &= ~MAP_ANONYMOUS;
  cb_params.on_resize = &clox_on_cb_resize;
  strncpy(cb_params.filename_prefix, "gc", sizeof(cb_params.filename_prefix));
  gc_state.scratch_cb = cb_create(&cb_params, sizeof(cb_params));
  if (!gc_state.scratch_cb) {
    fprintf(stderr, "Could not create GC's scratch continuous buffer. \n");
    return -1;
  }

  gc_state.visited_set_root = CB_BST_SENTINEL;
  gc_state.to_be_visited_root = CB_BST_SENTINEL;
  return 0;
}

struct merge_class_methods_closure
{
  struct cb         *src_cb;
  cb_offset_t        b_class_methods_bst;
  struct cb         *dest_cb;
  struct cb_region  *dest_region;
  cb_offset_t       *dest_methods_bst;
};

static int
merge_c_class_methods(const struct cb_term *key_term,
                      const struct cb_term *value_term,
                      void                 *closure)
{
  struct merge_class_methods_closure *cl = (struct merge_class_methods_closure *)closure;
  //Value keyValue = numToValue(cb_term_get_dbl(key_term));
  Value valueValue = numToValue(cb_term_get_dbl(value_term));
  struct cb_term temp_term;
  int ret;

  (void)ret;

  // No sense copying deleted/TOMBSTONE'd values.
  if (valueValue.val == TOMBSTONE_VAL.val)
    return 0;

  // The presence of an entry under this method name in the B class masks
  // our value (and will be subsequently copied if not a TOMBSTONE Value), so
  // no sense in copying it.
  if (cb_bst_lookup(cl->src_cb, cl->b_class_methods_bst, key_term, &temp_term) == 0)
    return 0;

  ret = cb_bst_insert(&(cl->dest_cb),
                      cl->dest_region,
                      cl->dest_methods_bst,
                      cb_region_start(cl->dest_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);
  assert(ret == 0);

  return 0;
}

static int
merge_b_class_methods(const struct cb_term *key_term,
                      const struct cb_term *value_term,
                      void                 *closure)
{
  struct merge_class_methods_closure *cl = (struct merge_class_methods_closure *)closure;
  //Value keyValue = numToValue(cb_term_get_dbl(key_term));
  Value valueValue = numToValue(cb_term_get_dbl(value_term));
  int ret;

  (void)ret;

  if (valueValue.val == TOMBSTONE_VAL.val)
    return 0;

  ret = cb_bst_insert(&(cl->dest_cb),
                      cl->dest_region,
                      cl->dest_methods_bst,
                      cb_region_start(cl->dest_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);
  assert(ret == 0);

  return 0;
}

struct merge_instance_fields_closure
{
  struct cb         *src_cb;
  cb_offset_t        b_instance_fields_bst;
  struct cb         *dest_cb;
  struct cb_region  *dest_region;
  cb_offset_t       *dest_fields_bst;
};

static int
merge_c_instance_fields(const struct cb_term *key_term,
                        const struct cb_term *value_term,
                        void                 *closure)
{
  struct merge_instance_fields_closure *cl = (struct merge_instance_fields_closure *)closure;
  //Value keyValue = numToValue(cb_term_get_dbl(key_term));
  Value valueValue = numToValue(cb_term_get_dbl(value_term));
  struct cb_term temp_term;
  int ret;

  (void)ret;

  // No sense copying deleted/TOMBSTONE'd values.
  if (valueValue.val == TOMBSTONE_VAL.val)
    return 0;

  // The presence of an entry under this method name in the B class masks
  // our value (and will be subsequently copied if not a TOMBSTONE Value), so
  // no sense in copying it.
  if (cb_bst_lookup(cl->src_cb, cl->b_instance_fields_bst, key_term, &temp_term) == 0)
    return 0;

  ret = cb_bst_insert(&(cl->dest_cb),
                      cl->dest_region,
                      cl->dest_fields_bst,
                      cb_region_start(cl->dest_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);
  assert(ret == 0);

  return 0;
}

static int
merge_b_instance_fields(const struct cb_term *key_term,
                        const struct cb_term *value_term,
                        void                 *closure)
{
  struct merge_instance_fields_closure *cl = (struct merge_instance_fields_closure *)closure;
  //Value keyValue = numToValue(cb_term_get_dbl(key_term));
  Value valueValue = numToValue(cb_term_get_dbl(value_term));
  int ret;

  (void)ret;

  if (valueValue.val == TOMBSTONE_VAL.val)
    return 0;

  ret = cb_bst_insert(&(cl->dest_cb),
                      cl->dest_region,
                      cl->dest_fields_bst,
                      cb_region_start(cl->dest_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);
  assert(ret == 0);

  return 0;
}

struct copy_objtable_closure
{
  struct cb        *src_cb;
  cb_offset_t       old_root_b;
  cb_offset_t       old_root_c;
  struct cb        *dest_cb;
  struct cb_region *dest_region;
  cb_offset_t      *new_root_c;
};

static int
copy_objtable_b(const struct cb_term *key_term,
                const struct cb_term *value_term,
                void                 *closure)
{
  //NOTE: This is simply copying the #ID -> @offset mapping into an
  // initially-blank cb_bst. It is not copying Objs.

  struct copy_objtable_closure *cl = (struct copy_objtable_closure *)closure;
  ObjID obj_id = { .id = cb_term_get_u64(key_term) };
  cb_offset_t offset = (cb_offset_t)cb_term_get_u64(value_term);
  int ret;

  //Skip those ObjIDs which have been invalidated.  (CB_NULL serves as a
  // tombstone in such cases).
  if (offset == CB_NULL)
    return 0;

  cb_offset_t c0 = cb_region_cursor(cl->dest_region);
  ret = cb_bst_insert(&(cl->dest_cb),
                      cl->dest_region,
                      cl->new_root_c,
                      cb_region_start(cl->dest_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);
  assert(ret == 0);
  cb_offset_t c1 = cb_region_cursor(cl->dest_region);
  printf("ACTUALINSERT1 +%ju bytes  #%ju -> @%ju\n",
         (uintmax_t)(c1 - c0),
         (uintmax_t)obj_id.id,
         (uintmax_t)offset);

  (void)ret;
  return 0;
}

static int
copy_objtable_c_not_in_b(const struct cb_term *key_term,
                         const struct cb_term *value_term,
                         void                 *closure)
{
  //NOTE: For #ObjID keys which do not exist in B, this is simply copying the
  // #ObjID -> @offset mapping into a cb_bst which already contains the entries
  // from B. However, for #ObjID keys which DO exist in B and which are for Objs
  // which have internal maps of their own (ObjClass's methods, and
  // ObjInstance's fields), a new Obj must be created to contain the merged set
  // of these contents.

  struct copy_objtable_closure *cl = (struct copy_objtable_closure *)closure;
  OID<Obj> objOID = (ObjID) { .id = cb_term_get_u64(key_term) };
  cb_offset_t cEntryOffset = (cb_offset_t)cb_term_get_u64(value_term);
  struct cb_term temp_term;
  cb_offset_t c0, c1;
  int ret;

  c0 = cb_region_cursor(cl->dest_region);

  // If an entry exists in both B and C, B's entry should mask C's EXCEPT when
  // the B entry and C entry can be merged (which is when they are both ObjClass
  // or both ObjInstance objects).
  if (cb_bst_lookup(cl->src_cb, cl->old_root_b, key_term, &temp_term) == 0) {
    cb_offset_t bEntryOffset = (cb_offset_t)cb_term_get_u64(&temp_term);
    CBO<Obj> bEntryObj = bEntryOffset;
    CBO<Obj> cEntryObj = cEntryOffset;

    if (bEntryObj.clp()->type == OBJ_CLASS && cEntryObj.clp()->type == OBJ_CLASS) {
      //1) Make a new ObjClass via mutableCopyObject.  methods set will be empty.
      //2) Copy C ObjClass's methods WHICH DO NOT EXIST IN B ObjClass's methods
      //   into the new ObjClass's methods set.
      //3) Copy B ObjClass's methods into this new ObjClass's methods set.
      //4) Insert this new merged object into the objtable.
      ObjClass *classB = (ObjClass *)bEntryObj.clp();
      ObjClass *classC = (ObjClass *)cEntryObj.clp();
      CBO<ObjClass> newObjClassCBO = mutableCopyObject(objOID.id(), objOID.co());
      struct merge_class_methods_closure subclosure;

      subclosure.src_cb              = cl->src_cb;
      subclosure.b_class_methods_bst = classB->methods_bst;
      subclosure.dest_cb             = cl->dest_cb;
      subclosure.dest_region         = cl->dest_region;
      subclosure.dest_methods_bst    = &(newObjClassCBO.mlp()->methods_bst);

      ret = cb_bst_traverse(cl->src_cb,
                            classC->methods_bst,
                            merge_c_class_methods,
                            &subclosure);
      assert(ret == 0);

      ret = cb_bst_traverse(cl->src_cb,
                            classB->methods_bst,
                            merge_b_class_methods,
                            &subclosure);
      assert(ret == 0);

      objtable_add_at(&thread_objtable, objOID.id(), newObjClassCBO.co());
    } else if (bEntryObj.clp()->type == OBJ_INSTANCE && cEntryObj.clp()->type == OBJ_INSTANCE) {
      //1) Make a new ObjInstance via mutableCopyObject.  fields set will be empty.
      //2) Copy C ObjInstance's fields WHICH DO NOT EXIST IN B ObjInstance's fields
      //   into the new ObjInstance's fields set.
      //3) Copy B ObjInstance's fields into this new ObjInstance's fields set.
      //4) Insert this new merged object into the objtable.
      ObjInstance *instanceB = (ObjInstance *)bEntryObj.clp();
      ObjInstance *instanceC = (ObjInstance *)cEntryObj.clp();
      CBO<ObjInstance> newObjInstanceCBO = mutableCopyObject(objOID.id(), objOID.co());
      struct merge_instance_fields_closure subclosure;

      subclosure.src_cb                = cl->src_cb;
      subclosure.b_instance_fields_bst = instanceB->fields_bst;
      subclosure.dest_cb               = cl->dest_cb;
      subclosure.dest_region           = cl->dest_region;
      subclosure.dest_fields_bst       = &(newObjInstanceCBO.mlp()->fields_bst);

      ret = cb_bst_traverse(cl->src_cb,
                            instanceC->fields_bst,
                            merge_c_instance_fields,
                            &subclosure);
      assert(ret == 0);

      ret = cb_bst_traverse(cl->src_cb,
                            instanceB->fields_bst,
                            merge_b_instance_fields,
                            &subclosure);
      assert(ret == 0);

      objtable_add_at(&thread_objtable, objOID.id(), newObjInstanceCBO.co());
    } else {
      // B's entry masks C's, so skip C's entry.
      // (We are presently traversing C's entries.)
      return 0;
    }
  }

  ret = cb_bst_insert(&(cl->dest_cb),
                      cl->dest_region,
                      cl->new_root_c,
                      cb_region_start(cl->dest_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);
  assert(ret == 0);

  c1 = cb_region_cursor(cl->dest_region);

  printf("ACTUALINSERT2 +%ju bytes #%ju -> @%ju\n",
         (uintmax_t)(c1 - c0),
         (uintmax_t)objOID.id().id,
         (uintmax_t)cEntryOffset);

  (void)ret;
  return 0;
}

int
gc_perform(struct gc_request *req, struct gc_response *resp)
{
  int ret;

  (void)ret;

  // Condense objtable
  {
    struct copy_objtable_closure closure;

    closure.src_cb      = req->orig_cb;
    closure.old_root_b  = req->objtable_root_b;
    closure.old_root_c  = req->objtable_root_c;
    closure.dest_cb     = req->orig_cb;
    closure.dest_region = &(req->objtable_new_region);
    closure.new_root_c  = &(resp->objtable_new_root_c);

    resp->objtable_new_root_c = CB_BST_SENTINEL;

    ret = cb_bst_traverse(req->orig_cb,
                          req->objtable_root_b,
                          copy_objtable_b,
                          &closure);
    printf("DANDEBUG done with copy_objtable_b() [s:%ju, c:%ju, e:%ju]\n",
           (uintmax_t)cb_region_start(&(req->objtable_new_region)),
           (uintmax_t)cb_region_cursor(&(req->objtable_new_region)),
           (uintmax_t)cb_region_end(&(req->objtable_new_region)));
    assert(ret == 0);

    ret = cb_bst_traverse(req->orig_cb,
                          req->objtable_root_c,
                          copy_objtable_c_not_in_b,
                          &closure);
    printf("DANDEBUG done with copy_objtable_c_not_in_b() [s:%ju, c:%ju, e:%ju]\n",
           (uintmax_t)cb_region_start(&(req->objtable_new_region)),
           (uintmax_t)cb_region_cursor(&(req->objtable_new_region)),
           (uintmax_t)cb_region_end(&(req->objtable_new_region)));
    assert(ret == 0);
  }

  //Condense tristack
  {
    cb_offset_t   new_cbo              = cb_region_start(&(req->tristack_new_region));
    Value        *new_condensed_values = static_cast<Value*>(cb_at(req->orig_cb, new_cbo));
    Value        *old_c_values         = static_cast<Value*>(cb_at(req->orig_cb, req->tristack_cbo));
    Value        *old_b_values         = static_cast<Value*>(cb_at(req->orig_cb, req->tristack_bbo));
    unsigned int  i                    = req->tristack_cbi;  //(always 0, really)

    assert(i == 0);

    //Copy C section
    while (i < req->tristack_stackDepth && i < req->tristack_bbi) {
      new_condensed_values[i] = old_c_values[i - req->tristack_cbi];
      ++i;
    }

    //Copy B section
    while (i < req->tristack_stackDepth && i < req->tristack_abi) {
      new_condensed_values[i] = old_b_values[i - req->tristack_bbi];
      ++i;
    }

    //Write response
    resp->tristack_new_cbo = new_cbo;
    resp->tristack_new_cbi = req->tristack_cbi;
  }

  return 0;
}
