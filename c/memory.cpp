#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cb_bst.h>
#include <cb_region.h>

#include "cb_integration.h"
#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_TRACE_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

static int gciteration = 0;


size_t
alloc_size_get(const char *mem) {
  size_t size;
  memcpy(&size, mem - sizeof(size_t), sizeof(size_t));
  return size;
}

static void
alloc_size_set(char *mem, size_t size) {
  memcpy(mem - sizeof(size_t), &size, sizeof(size_t));
}

size_t
alloc_alignment_get(const char *mem) {
  size_t alignment;
  memcpy(&alignment, mem - (2 * sizeof(size_t)), sizeof(size_t));
  return alignment;
}

static void
alloc_alignment_set(char *mem, size_t alignment) {
  memcpy(mem - (2 * sizeof(size_t)), &alignment, sizeof(size_t));
}

bool
alloc_is_object_get(const char *mem) {
  size_t is_object;
  memcpy(&is_object, mem - (2 * sizeof(size_t) + sizeof(bool)), sizeof(bool));
  return is_object;
}

static void
alloc_is_object_set(char *mem, bool is_object) {
  memcpy(mem - (2 * sizeof(size_t) + sizeof(bool)) , &is_object, sizeof(bool));
}

static inline void
clobber_mem(void *p, size_t len) {
#ifdef DEBUG_TRACE_GC
    memset(p, '!', len);
#endif
}

cb_offset_t reallocate_within(struct cb **cb, struct cb_region *region, cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment, bool isObject, bool suppress_gc) {
  vm.bytesAllocated += newSize - oldSize;

  if (!suppress_gc) {
    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
      collectGarbage();
#endif

      if (vm.bytesAllocated > vm.nextGC) {
        collectGarbage();
      }
    }
  }

#ifdef DEBUG_TRACE_GC
  if (previous != CB_NULL) {
    //Check that old values are as expected, given that this function
    //historically has expected to be given the old size and alignments
    //cannot change over reallocate()s.
    char *mem = (char *)cb_at(*cb, previous);
    assert(alloc_size_get(mem) == oldSize);
    assert(alloc_alignment_get(mem) == alignment);
    assert(alloc_is_object_get(mem) == isObject);
    (void)mem;
  }
#endif

  if (newSize == 0) {
    clobber_mem(cb_at(*cb, previous), oldSize);
    return CB_NULL;
  } else if (newSize < oldSize) {
    clobber_mem(((char *)cb_at(*cb, previous)) + newSize, oldSize - newSize);
    return previous;
  } else {
    size_t header_size = sizeof(size_t)   /* size field */
                         + sizeof(size_t) /* alignment field */
                         + sizeof(bool);  /* isObject field */
    size_t needed_contiguous_size = header_size + (alignment - 1) + newSize;
    cb_offset_t new_offset;
    int ret;

    ret = cb_region_memalign(cb,
                             region,
                             &new_offset,
                             alignment,
                             needed_contiguous_size);
    if (ret != CB_SUCCESS) {
      return CB_NULL;
    }
    new_offset = cb_offset_aligned_gte(new_offset + header_size, alignment);

    char *mem = (char *)cb_at(*cb, new_offset);
    alloc_size_set(mem, newSize);
    alloc_alignment_set(mem, alignment);
    alloc_is_object_set(mem, isObject);

    //Q: Should we keep the ObjID the same over reallocation?
    //A: No, changing it adheres to the earlier API which expects a shift of
    // offset (or earlier API than that, pointer).  Although it may work to
    // leave the ObjID the same, this may gloss over errors elsewhere, so we
    // force it to change for the sake of provoking any such errors.
    if (previous != CB_NULL) {
      char *prevMem = (char *)cb_at(*cb, previous);
      memcpy(mem, prevMem, oldSize);
      clobber_mem(prevMem, oldSize);
    }

    return new_offset;
  }
}

cb_offset_t reallocate(cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment, bool isObject, bool suppress_gc) {
  return reallocate_within(&thread_cb, &thread_region, previous, oldSize, newSize, alignment, isObject, suppress_gc);
}

bool objectIsDark(const OID<Obj> objectOID) {
  cb_term key_term;

  cb_term_set_u64(&key_term, objectOID.id().id);

  return cb_bst_contains_key(gc_thread_cb,
                             gc_thread_darkset_bst,
                             &key_term);
}

static void objectSetDark(OID<Obj> objectOID) {
  cb_term key_term;
  cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, objectOID.id().id);
  cb_term_set_u64(&value_term, objectOID.id().id);

  ret = cb_bst_insert(&gc_thread_cb,
                      &gc_thread_region,
                      &gc_thread_darkset_bst,
                      0,
                      &key_term,
                      &value_term);
  assert(ret == 0);
  (void)ret;

  //NOTE: We cannot validate growth vs. estimated max growth in this function
  // because we are not allocating into a static region whose size is known
  // ahead of time.  As such, regions may be being created dynamically to
  // fulfill the insertion, and region cursor positions can therefore differ
  // by large amounts unrelated to actual bst insertion allocations.
}

static void clearDarkObjectSet(void) {
  gc_thread_darkset_bst = CB_BST_SENTINEL;
}

void grayObject(const OID<Obj> objectOID) {
  if (objectOID.is_nil()) return;

  // Don't get caught in cycle.
  if (objectIsDark(objectOID)) return;

#ifdef DEBUG_TRACE_GC
  printf("#%ju grayObject() ", (uintmax_t)objectOID.id().id);
  printValue(OBJ_VAL(objectOID.id()));
  printf("\n");
#endif

  objectSetDark(objectOID);

  if (vm.grayCapacity < vm.grayCount + 1) {
    int oldGrayCapacity = vm.grayCapacity;
    cb_offset_t oldGrayStackOffset = vm.grayStack.co();
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);

    vm.grayStack = reallocate_within(&gc_thread_cb,
                                     &gc_thread_region,
                                     oldGrayStackOffset,
                                     sizeof(OID<Obj>) * oldGrayCapacity,
                                     sizeof(OID<Obj>) * vm.grayCapacity,
                                     cb_alignof(OID<Obj>),
                                     false,
                                     true);

#ifdef DEBUG_TRACE_GC
    printf("@%ju OID<Obj>[%zd] array allocated (%zd bytes) (resized from @%ju OID<Obj>[%zd] array (%zd bytes))\n",
           (uintmax_t)vm.grayStack.co(),
           (size_t)vm.grayCapacity,
           sizeof(OID<Obj*>) * vm.grayCapacity,
           (uintmax_t)oldGrayStackOffset,
           (size_t)oldGrayCapacity,
           sizeof(OID<Obj*>) * oldGrayCapacity);
#endif

  }

  vm.grayStack.mrp(gc_thread_cb)[vm.grayCount++] = objectOID;
}

void grayValue(Value value) {
  if (!IS_OBJ(value)) return;
  grayObject(AS_OBJ_ID(value));
}

static bool isWhiteObject(OID<Obj> objectOID) {
  if (objectOID.is_nil()) return true;
  if (objectIsDark(objectOID)) return false;
  return true;
}

bool isWhite(Value value) {
  if (!IS_OBJ(value)) return true;
  return isWhiteObject(AS_OBJ_ID(value));
}

static int
bstTraversalGray(const struct cb_term *key_term,
                 const struct cb_term *value_term,
                 void                 *closure)
{
  Value keyValue;
  Value valueValue;

  keyValue = numToValue(cb_term_get_dbl(key_term));
  valueValue = numToValue(cb_term_get_dbl(value_term));

  grayValue(keyValue);
  grayValue(valueValue);

  return 0;
}

static void grayBst(cb_offset_t bst) {
  int ret;

  (void)ret;

  ret = cb_bst_traverse(thread_cb,
                        bst,
                        &bstTraversalGray,
                        NULL);
  assert(ret == 0);
}

static void grayObjectLeaves(const OID<Obj> objectOID) {
  const Obj *object;
  bool found_in_b;

  //There should never be any A region contents during this call.
  assert(!objectOID.clipA());

  //Find the Obj whose leaves are to be darkened in either the B or C region.
  object = objectOID.clipB();
  found_in_b = !!object;
  if (!found_in_b) object = objectOID.clipC();

#ifdef DEBUG_TRACE_GC
  printf("#%ju grayObjectLeaves() ", (uintmax_t)objectOID.id().id);
  printValue(OBJ_VAL(objectOID.id()));
  printf("\n");
#endif

  switch (object->type) {
    case OBJ_BOUND_METHOD: {
      const ObjBoundMethod* bound = (const ObjBoundMethod*)object;
      grayValue(bound->receiver);
      grayObject(bound->method.id());
      break;
    }

    case OBJ_CLASS: {
      const ObjClass* klass = (const ObjClass*)object;
      grayObject(klass->name.id());
      grayObject(klass->superclass.id());
      grayBst(klass->methods_bst);

      //NOTE: Classes are represented by ObjClass layers.  The garbage
      // collector only deals with regions B and C.  If retrieval of the
      // objectOID has given us a B region ObjClass layer, then also gray any
      // methods of any backing C region ObjClass layer.
      if (found_in_b) {
        const ObjClass* klass_C = (const ObjClass*)objectOID.clipC();
        if (klass_C) {
          printf("DANDEBUG FOUND BACKING CLASS\n");
          grayBst(klass_C->methods_bst);
        }
      }
      break;
    }

    case OBJ_CLOSURE: {
      const ObjClosure* closure = (const ObjClosure*)object;
      grayObject(closure->function.id());
      for (int i = 0; i < closure->upvalueCount; i++) {
        grayObject(closure->upvalues.clp()[i].id());
      }
      break;
    }

    case OBJ_FUNCTION: {
      const ObjFunction* function = (const ObjFunction*)object;
      grayObject(function->name.id());
      for (int i = 0; i < function->chunk.constants.count; i++) {
        grayValue(function->chunk.constants.values.clp()[i]);
      }
      break;
    }

    case OBJ_INSTANCE: {
      const ObjInstance* instance = (const ObjInstance*)object;
      grayObject(instance->klass.id());
      grayBst(instance->fields_bst);

      //NOTE: Instances are represented by ObjInstance layers.  The garbage
      // collector only deals with regions B and C.  If retrieval of the
      // objectOID has given us a B region ObjInstance layer, then also gray any
      // fields of any backing C region ObjInstance layer.
      if (found_in_b) {
        const ObjInstance* instance_C = (const ObjInstance*)objectOID.clipC();
        if (instance_C) {
          printf("DANDEBUG FOUND BACKING INSTANCE\n");
          grayBst(instance_C->fields_bst);
        }
      }
      break;
    }

    case OBJ_UPVALUE:
      grayValue(((const ObjUpvalue*)object)->closed);
      break;

    case OBJ_NATIVE:
    case OBJ_STRING:
      // No references.
      break;
  }
}

static void freeObject(OID<Obj> object) {
//> Garbage Collection not-yet
#ifdef DEBUG_TRACE_GC
  printf("#%ju freeObject() ", (uintmax_t)object.id().id);
  printValue(OBJ_VAL(object.id()));
  printf("\n");
#endif

  //NOTE: We can no longer clobber old memory contents due to the fact that
  // older maps (e.g. regions B and C of objtable) may still be working with
  // the objects.  Instead, freeObject() now just means to nullify the referred
  // #ID, so that it can no longer be dereferenced via the runtime of the
  // executing program and will eventually have no @offset associated with it in
  // any region of the objtable map.
  objtable_invalidate(&thread_objtable, object.id());
}

cb_offset_t deriveMutableObjectLayer(struct cb **cb, struct cb_region *region, ObjID id, cb_offset_t object_offset) {
  PIN_SCOPE;
  CBO<Obj> srcCBO = object_offset;
  CBO<Obj> destCBO;
  bool suppress_gc = (gc_phase != GC_PHASE_NORMAL_EXEC);  //prevent cycles

  printf("#%ju@%ju deriveMutableObjectLayer() ", (uintmax_t)id.id, object_offset);
  printObject(id, object_offset, srcCBO.crp(*cb));
  printf("\n");

  switch (srcCBO.crp(*cb)->type) {
    case OBJ_BOUND_METHOD: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjBoundMethod), cb_alignof(ObjBoundMethod), true, suppress_gc);
      ObjBoundMethod       *dest = (ObjBoundMethod *)destCBO.mrp(*cb);
      const ObjBoundMethod *src  = (const ObjBoundMethod *)srcCBO.crp(*cb);

      dest->obj      = src->obj;
      dest->receiver = src->receiver;
      dest->method   = src->method;

      break;
    }

    case OBJ_CLASS: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjClass), cb_alignof(ObjClass), true, suppress_gc);
      ObjClass       *dest = (ObjClass *)destCBO.mrp(*cb);
      const ObjClass *src  = (const ObjClass *)srcCBO.crp(*cb);

      dest->obj         = src->obj;
      dest->name        = src->name;
      dest->superclass  = src->superclass;
      //NOTE: We expect lookup of methods to first check this new, mutable,
      //  A-region ObjClass, before looking at older versions in B and C.
      methods_layer_init(cb, region, &(dest->methods_bst));
      break;
    }

    case OBJ_CLOSURE: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjClosure), cb_alignof(ObjClosure), true, suppress_gc);
      ObjClosure *dest      = (ObjClosure *)destCBO.mrp(*cb);
      const ObjClosure *src = (const ObjClosure *)srcCBO.crp(*cb);

      dest->obj          = src->obj;
      dest->function     = src->function;
      dest->upvalues     = GROW_ARRAY_NOGC_WITHIN(cb, region, CB_NULL, OID<ObjUpvalue>, 0, src->upvalueCount);
      {
        const OID<ObjUpvalue> *srcUpvalues = src->upvalues.crp(*cb);
        OID<ObjUpvalue> *destUpvalues = dest->upvalues.mrp(*cb);

        for (unsigned int i = 0, e = src->upvalueCount; i < e; ++i) {
          //printf("DANDEBUG copying upvalue (srcUpvalues:%p, srcUpvalues[%d].id(): %ju, srcUpvalues[%d].clip():%p\n",
          //    srcUpvalues, i, (uintmax_t)srcUpvalues[i].id().id, i, srcUpvalues[i].clip());
          //printValue(srcUpvalues[i].clip()->closed);
          //printf("\n");
          destUpvalues[i] = srcUpvalues[i];
        }
      }
      dest->upvalueCount = src->upvalueCount;

      break;
    }

    case OBJ_FUNCTION: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjFunction), cb_alignof(ObjFunction), true, suppress_gc);
      ObjFunction       *dest = (ObjFunction *)destCBO.mrp(*cb);
      const ObjFunction *src  = (const ObjFunction *)srcCBO.crp(*cb);

      dest->obj          = src->obj;
      dest->arity        = src->arity;
      dest->upvalueCount = src->upvalueCount;
      dest->chunk.count    = src->chunk.count;
      dest->chunk.capacity = src->chunk.capacity;
      dest->chunk.code = GROW_ARRAY_NOGC_WITHIN(cb, region, CB_NULL, uint8_t, 0, src->chunk.capacity);
      memcpy(dest->chunk.code.mrp(*cb), src->chunk.code.crp(*cb), src->chunk.capacity * sizeof(uint8_t));
      dest->chunk.lines = GROW_ARRAY_NOGC_WITHIN(cb, region, CB_NULL, int, 0, src->chunk.capacity);
      memcpy(dest->chunk.lines.mrp(*cb), src->chunk.lines.crp(*cb), src->chunk.capacity * sizeof(int));
      dest->chunk.constants.capacity = src->chunk.constants.capacity;
      dest->chunk.constants.count    = src->chunk.constants.count;
      dest->chunk.constants.values   = GROW_ARRAY_NOGC_WITHIN(cb, region, CB_NULL, Value, 0, src->chunk.constants.capacity);
      memcpy(dest->chunk.constants.values.mrp(*cb), src->chunk.constants.values.crp(*cb), src->chunk.constants.capacity * sizeof(Value));
      dest->name         = src->name;

      break;
    }

    case OBJ_INSTANCE: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjInstance), cb_alignof(ObjInstance), true, suppress_gc);
      ObjInstance       *dest = (ObjInstance *)destCBO.mrp(*cb);
      const ObjInstance *src  = (const ObjInstance *)srcCBO.crp(*cb);

      dest->obj        = src->obj;
      dest->klass      = src->klass;
      //NOTE: We expect lookup of fields to first check this new, mutable,
      //  A-region ObjClass, before looking at older versions in B and C.
      fields_layer_init(cb, region, &(dest->fields_bst));
      break;
    }

    case OBJ_NATIVE: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjNative), cb_alignof(ObjNative), true, suppress_gc);
      ObjNative       *dest = (ObjNative *)destCBO.mrp(*cb);
      const ObjNative *src  = (const ObjNative *)srcCBO.crp(*cb);

      dest->obj      = src->obj;
      dest->function = src->function;

      break;
    }

    case OBJ_STRING: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjString), cb_alignof(ObjString), true, suppress_gc);
      ObjString       *dest = (ObjString *)destCBO.mrp(*cb);
      const ObjString *src  = (const ObjString *)srcCBO.crp(*cb);

      dest->obj    = src->obj;
      dest->length = src->length;
      dest->chars  = GROW_ARRAY_NOGC_WITHIN(cb, region, CB_NULL, char, 0, src->length + 1);
      memcpy(dest->chars.mrp(*cb), src->chars.crp(*cb), src->length * sizeof(char));
      dest->chars.mrp(*cb)[src->length] = '\0';
      dest->hash   = src->hash;

      break;
    }

    case OBJ_UPVALUE: {
      destCBO = reallocate_within(cb, region, CB_NULL, 0, sizeof(ObjUpvalue), cb_alignof(ObjUpvalue), true, suppress_gc);
      ObjUpvalue       *dest = (ObjUpvalue *)destCBO.mrp(*cb);
      const ObjUpvalue *src  = (const ObjUpvalue *)srcCBO.crp(*cb);

      dest->obj             = src->obj;
      dest->valueStackIndex = src->valueStackIndex;
      dest->closed          = src->closed;
      dest->next            = src->next;

      break;
    }
  }

  return destCBO.mo();
}

struct copy_entry_closure
{
  struct cb        **dest_cb;
  struct cb_region  *dest_region;
  cb_offset_t       *dest_bst;
  size_t             last_s;
};

static int
copy_entry_to_bst(const struct cb_term *key_term,
                  const struct cb_term *value_term,
                  void                 *closure)
{
  struct copy_entry_closure *cl = (struct copy_entry_closure *)closure;
  cb_offset_t c0, c1;
  size_t s1;
  int ret;

  (void)ret;

  c0 = cb_region_cursor(cl->dest_region);

  ret = cb_bst_insert(cl->dest_cb,
                      cl->dest_region,
                      cl->dest_bst,
                      cb_region_start(cl->dest_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);

  assert(ret == 0);
  c1 = cb_region_cursor(cl->dest_region);
  s1 = cb_bst_size(*(cl->dest_cb), *cl->dest_bst);

  // Actual bytes used must be <= reported bytes.
  assert(c1 - c0 <= s1 - cl->last_s);
  cl->last_s = s1;

  return 0;
}

cb_offset_t cloneObject(struct cb **cb, struct cb_region *region, ObjID id, cb_offset_t object_offset) {
  assert(gc_phase == GC_PHASE_CONSOLIDATE);

  CBO<Obj> srcCBO = object_offset;
  CBO<Obj> cloneCBO = deriveMutableObjectLayer(cb, region, id, object_offset);
  int ret;

  (void)ret;

  printf("#%ju@%ju cloneObject() ", (uintmax_t)id.id, object_offset);
  printObject(id, object_offset, srcCBO.clp());
  printf(" : NEW OFFSET = %ju\n", (uintmax_t)cloneCBO.mo());

  //NOTE: ObjClasses and ObjInstances come out of deriveMutableObjectLayer()
  // without contents in their respective methods_bst/fields_bst, so the
  // contents must be copied in separately here.
  switch (srcCBO.clp()->type) {
    case OBJ_CLASS: {
      ObjClass *srcClass = (ObjClass *)srcCBO.crp(*cb);
      ObjClass *destClass = (ObjClass *)cloneCBO.crp(*cb);
      struct copy_entry_closure cl = { .dest_cb = cb, .dest_region = region, .dest_bst = &(destClass->methods_bst), .last_s = cb_bst_size(*cb, destClass->methods_bst) };

      ret = cb_bst_traverse(*cb,
                            srcClass->methods_bst,
                            copy_entry_to_bst,
                            &cl);
      assert(ret == 0);
    }
    break;

    case OBJ_INSTANCE: {
      ObjInstance *srcInstance = (ObjInstance *)srcCBO.crp(*cb);
      ObjInstance *destInstance = (ObjInstance *)cloneCBO.crp(*cb);
      struct copy_entry_closure cl = { .dest_cb = cb, .dest_region = region, .dest_bst = &(destInstance->fields_bst), .last_s = cb_bst_size(*cb, destInstance->fields_bst) };

      ret = cb_bst_traverse(*cb,
                            srcInstance->fields_bst,
                            copy_entry_to_bst,
                            &cl);
      assert(ret == 0);
    }
    break;

    default:
      break;
  }

  return cloneCBO.mo();
}

void freezeARegions(cb_offset_t new_lower_bound) {
  int ret;

  (void)ret;

  // Objtable
  assert(cb_bst_num_entries(thread_cb, thread_objtable.root_c) == 0);
  thread_objtable.root_c = thread_objtable.root_b;
  thread_objtable.root_b = thread_objtable.root_a;
  ret = objtable_layer_init(&thread_cb, &thread_region, &(thread_objtable.root_a));
  assert(ret == 0);
  assert(thread_objtable.root_a >= new_lower_bound);

  // Tristack
  assert(vm.tristack.cbo == CB_NULL);
  assert(vm.tristack.cbi == 0);
  vm.tristack.cbo = vm.tristack.bbo;
  vm.tristack.cbi = vm.tristack.bbi;
  vm.tristack.bbo = vm.tristack.abo;
  vm.tristack.bbi = vm.tristack.abi;
  ret = cb_region_memalign(&thread_cb,
                           &thread_region,
                           &(vm.tristack.abo),
                           cb_alignof(Value),
                           sizeof(Value) * STACK_MAX);
  vm.tristack.abi = vm.tristack.stackDepth;
  assert(vm.tristack.abo >= new_lower_bound);

  // Triframes
  assert(vm.triframes.cbo == CB_NULL);
  assert(vm.triframes.cbi == 0);
  vm.triframes.cbo = vm.triframes.bbo;
  vm.triframes.cbi = vm.triframes.bbi;
  vm.triframes.bbo = vm.triframes.abo;
  vm.triframes.bbi = vm.triframes.abi;
  ret = cb_region_memalign(&thread_cb,
                           &thread_region,
                           &(vm.triframes.abo),
                           cb_alignof(CallFrame),
                           sizeof(CallFrame) * FRAMES_MAX);
  vm.triframes.abi = vm.triframes.frameCount;
  assert(vm.triframes.abo >= new_lower_bound);

  // Strings
  assert(cb_bst_num_entries(thread_cb, vm.strings.root_c) == 0);
  vm.strings.root_c = vm.strings.root_b;
  vm.strings.root_b = vm.strings.root_a;
  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(vm.strings.root_a),
                    &clox_value_deep_comparator,
                    &clox_value_deep_comparator,
                    &clox_value_render,
                    &clox_value_render,
                    &clox_no_external_size,
                    &clox_no_external_size);
  assert(ret == 0);
  assert(vm.strings.root_a >= new_lower_bound);

  // Globals
  assert(cb_bst_num_entries(thread_cb, vm.globals.root_c) == 0);
  vm.globals.root_c = vm.globals.root_b;
  vm.globals.root_b = vm.globals.root_a;
  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(vm.globals.root_a),
                    &clox_value_deep_comparator,
                    &clox_value_deep_comparator,
                    &clox_value_render,
                    &clox_value_render,
                    &clox_no_external_size,
                    &clox_no_external_size);
  assert(ret == 0);
  assert(vm.globals.root_a >= new_lower_bound);
}

void collectGarbageCB(cb_offset_t new_lower_bound) {
  struct gc_request req;
  struct gc_response resp;
  int ret;

#ifdef DEBUG_TRACE_GC
  printf("-- BEGIN CB GC %d\n", gciteration);
#endif

  memset(&req, 0, sizeof(req));
  memset(&resp, 0, sizeof(resp));

  gc_phase = GC_PHASE_PREPARE_REQUEST;

  //Prepare request contents
  req.orig_cb = thread_cb;

  // Prepare condensing objtable B+C
  size_t objtable_b_size = cb_bst_size(thread_cb, thread_objtable.root_b);
  size_t objtable_c_size = cb_bst_size(thread_cb, thread_objtable.root_c);
  printf("DANDEBUG objtable_b_size: %zd, objtable_c_size: %zd\n",
         objtable_b_size, objtable_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.objtable_new_region,
                         CB_CACHE_LINE_SIZE,
                         objtable_b_size + objtable_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  assert(cb_region_start(&(req.objtable_new_region)) >= new_lower_bound);
  req.objtable_root_b = thread_objtable.root_b;
  req.objtable_root_c = thread_objtable.root_c;

  // Prepare condensing tristack B+C
  size_t tristack_b_plus_c_size = sizeof(Value) * (vm.tristack.abi - vm.tristack.cbi);
  printf("DANDEBUG tristack_b_plus_c_size: %zd\n", tristack_b_plus_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.tristack_new_region,
                         CB_CACHE_LINE_SIZE,
                         tristack_b_plus_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  assert(cb_region_start(&(req.tristack_new_region)) >= new_lower_bound);
  req.tristack_abi        = vm.tristack.abi;
  req.tristack_bbo        = vm.tristack.bbo;
  req.tristack_bbi        = vm.tristack.bbi;
  req.tristack_cbo        = vm.tristack.cbo;
  req.tristack_cbi        = vm.tristack.cbi;
  req.tristack_stackDepth = vm.tristack.stackDepth;

  // Prepare condensing triframes B+C
  size_t triframes_b_plus_c_size = sizeof(CallFrame) * (vm.triframes.abi - vm.triframes.cbi);
  printf("DANDEBUG triframes_b_plus_c_size: %zd\n", triframes_b_plus_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.triframes_new_region,
                         CB_CACHE_LINE_SIZE,
                         triframes_b_plus_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  assert(cb_region_start(&(req.triframes_new_region)) >= new_lower_bound);
  req.triframes_abi        = vm.triframes.abi;
  req.triframes_bbo        = vm.triframes.bbo;
  req.triframes_bbi        = vm.triframes.bbi;
  req.triframes_cbo        = vm.triframes.cbo;
  req.triframes_cbi        = vm.triframes.cbi;
  req.triframes_frameCount = vm.triframes.frameCount;


  // Prepare condensing strings B+C
  size_t strings_b_size = cb_bst_size(thread_cb, vm.strings.root_b);
  size_t strings_c_size = cb_bst_size(thread_cb, vm.strings.root_c);
  printf("DANDEBUG strings_b_size: %zd, strings_c_size: %zd\n",
         strings_b_size, strings_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.strings_new_region,
                         CB_CACHE_LINE_SIZE,
                         strings_b_size + strings_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  assert(cb_region_start(&(req.strings_new_region)) >= new_lower_bound);
  req.strings_root_b = vm.strings.root_b;
  req.strings_root_c = vm.strings.root_c;

  // Prepare condensing globals B+C
  size_t globals_b_size = cb_bst_size(thread_cb, vm.globals.root_b);
  size_t globals_c_size = cb_bst_size(thread_cb, vm.globals.root_c);
  printf("DANDEBUG globals_b_size: %zd, globals_c_size: %zd\n",
         globals_b_size, globals_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.globals_new_region,
                         CB_CACHE_LINE_SIZE,
                         globals_b_size + globals_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  assert(cb_region_start(&(req.globals_new_region)) >= new_lower_bound);
  req.globals_root_b = vm.globals.root_b;
  req.globals_root_c = vm.globals.root_c;


  gc_phase = GC_PHASE_CONSOLIDATE;
  //Do the Garbage Collection / Condensing.
  ret = gc_perform(&req, &resp);
  if (ret != 0) {
    fprintf(stderr, "Failed to GC via CB.\n");
  }
  assert(ret == 0);

  gc_phase = GC_PHASE_INTEGRATE_RESULT;
  //Integrate condensed objtable.
  printf("DANDEBUG objtable C %ju -> %ju\n", (uintmax_t)thread_objtable.root_c, (uintmax_t)CB_BST_SENTINEL);
  printf("DANDEBUG objtable B %ju -> %ju\n", (uintmax_t)thread_objtable.root_b, (uintmax_t)resp.objtable_new_root_b);
  thread_objtable.root_c = CB_BST_SENTINEL;
  thread_objtable.root_b = resp.objtable_new_root_b;
  assert(thread_objtable.root_b >= new_lower_bound);
  assert(thread_objtable.root_a >= new_lower_bound);

  //Integrate condensed tristack.
  //printf("BEFORE CONDENSING TRISTACK\n");
  //tristack_print(&(vm.tristack));
  vm.tristack.cbo = CB_NULL;
  vm.tristack.cbi = 0;
  vm.tristack.bbo = resp.tristack_new_bbo;
  assert(resp.tristack_new_bbi == 0);
  vm.tristack.bbi = resp.tristack_new_bbi;
  //printf("AFTER CONDENSING TRISTACK\n");
  //tristack_print(&(vm.tristack));
  //assert(vm.tristack.cbo >= new_lower_bound);
  assert(vm.tristack.bbo >= new_lower_bound);
  assert(vm.tristack.abo >= new_lower_bound);

  //Integrate condensed triframes.
  printf("DANDEBUG before integrating triframes  abo: %ju, abi: %ju, bbo: %ju, bbi: %ju, cbo: %ju, cbi: %ju\n",
      (uintmax_t)vm.triframes.abo,
      (uintmax_t)vm.triframes.abi,
      (uintmax_t)vm.triframes.bbo,
      (uintmax_t)vm.triframes.bbi,
      (uintmax_t)vm.triframes.cbo,
      (uintmax_t)vm.triframes.cbi);
  triframes_print(&(vm.triframes));
  vm.triframes.cbo = CB_NULL;
  vm.triframes.cbi = 0;
  vm.triframes.bbo = resp.triframes_new_bbo;
  vm.triframes.bbi = resp.triframes_new_bbi;
  //assert(vm.triframes.cbo >= new_lower_bound);
  assert(vm.triframes.bbo >= new_lower_bound);
  assert(vm.triframes.abo >= new_lower_bound);
  printf("DANDEBUG after integrating triframes  abo: %ju, abi: %ju, bbo: %ju, bbi: %ju, cbo: %ju, cbi: %ju\n",
      (uintmax_t)vm.triframes.abo,
      (uintmax_t)vm.triframes.abi,
      (uintmax_t)vm.triframes.bbo,
      (uintmax_t)vm.triframes.bbi,
      (uintmax_t)vm.triframes.cbo,
      (uintmax_t)vm.triframes.cbi);
  triframes_print(&(vm.triframes));
  triframes_ensureCurrentFrameIsMutable(&vm.triframes);
  printf("DANDEBUG after ensuring last frame is mutable: abo: %ju, abi: %ju, bbo: %ju, bbi: %ju, cbo: %ju, cbi: %ju\n",
      (uintmax_t)vm.triframes.abo,
      (uintmax_t)vm.triframes.abi,
      (uintmax_t)vm.triframes.bbo,
      (uintmax_t)vm.triframes.bbi,
      (uintmax_t)vm.triframes.cbo,
      (uintmax_t)vm.triframes.cbi);
  triframes_print(&(vm.triframes));

  //Integrate condensed strings.
  vm.strings.root_c = CB_BST_SENTINEL;
  vm.strings.root_b = resp.strings_new_root_b;
  assert(vm.strings.root_b >= new_lower_bound);
  assert(vm.strings.root_a >= new_lower_bound);

  //Integrate condensed globals.
  vm.globals.root_c = CB_BST_SENTINEL;
  vm.globals.root_b = resp.globals_new_root_b;
  assert(vm.globals.root_b >= new_lower_bound);
  assert(vm.globals.root_a >= new_lower_bound);

  if (vm.currentFrame)
    vm.currentFrame->slots = tristack_at(&(vm.tristack), vm.currentFrame->slotsIndex);

  // Collect the white objects.
  gc_phase = GC_PHASE_FREE_WHITE_SET;
  OID<struct sObj> white_list = resp.white_list;
  // Take off white objects from the front of the vm.objects list.
  while (!white_list.is_nil()) {
    OID<Obj> unreached = white_list;
    white_list = white_list.clip()->white_next;
    freeObject(unreached);
  }

#ifdef DEBUG_TRACE_GC
  printf("-- END CB GC %d\n", gciteration);
#endif
}

static int
printObjtableTraversal(const struct cb_term *key_term,
                       const struct cb_term *value_term,
                       void                 *closure)
{
  const char *desc = (const char *)closure;
  ObjID objID = { .id = cb_term_get_u64(key_term) };
  cb_offset_t offset = (cb_offset_t)cb_term_get_u64(value_term);

  printf("%s #%ju -> @%ju\n",
         desc,
         (uintmax_t)objID.id,
         (uintmax_t)offset);

  return 0;
}

static int
grayObjtableTraversal(const struct cb_term *key_term,
                      const struct cb_term *value_term,
                      void                 *closure)
{
  const char *desc = (const char *)closure;
  ObjID objID = { .id = cb_term_get_u64(key_term) };

  printf("%s graying #%ju\n",
         desc,
         (uintmax_t)objID.id);

  grayObject(objID);

  return 0;
}

void printStateOfWorld(const char *desc) {
  int ret;

  (void)ret;

  printf("===== BEGIN STATE OF WORLD %s (gc: %d) =====\n", desc, gciteration);

  printf("----- begin objtable (a:%ju, asz:%zu, b:%ju, bsz:%zu, c:%ju, csz:%zu)-----\n",
         thread_objtable.root_a,
         (size_t)cb_bst_size(thread_cb, thread_objtable.root_a),
         thread_objtable.root_b,
         (size_t)cb_bst_size(thread_cb, thread_objtable.root_b),
         thread_objtable.root_c,
         (size_t)cb_bst_size(thread_cb, thread_objtable.root_b));
  ret = cb_bst_traverse(thread_cb,
                        thread_objtable.root_a,
                        &printObjtableTraversal,
                        (void*)"A");
  assert(ret == 0);
  ret = cb_bst_traverse(thread_cb,
                        thread_objtable.root_b,
                        &printObjtableTraversal,
                        (void*)"B");
  assert(ret == 0);
  ret = cb_bst_traverse(thread_cb,
                        thread_objtable.root_c,
                        &printObjtableTraversal,
                        (void*)"C");
  assert(ret == 0);
  printf("----- end objtable -----\n");

  printf("----- begin vm.strings -----\n");
  printTable(&vm.strings, "vm.strings");
  printf("----- end vm.strings -----\n");

  printf("----- begin vm.globals -----\n");
  printTable(&vm.globals, "vm.globals");
  printf("----- end vm.globals -----\n");

  printf("----- begin vm.tristack -----\n");
  tristack_print(&(vm.tristack));
  printf("----- end vm.tristack -----\n");

  printf("----- begin vm.triframes -----\n");
  triframes_print(&(vm.triframes));
  printf("----- end vm.triframes -----\n");

  printf("----- begin vm.openUpvalues -----\n");
  for (OID<ObjUpvalue> upvalue = vm.openUpvalues;
       !upvalue.is_nil();
       upvalue = upvalue.clip()->next) {
    printObject(upvalue.id(), upvalue.co(), (const Obj *)upvalue.clip());
    printf("\n");
  }
  printf("----- end vm.openUpvalues -----\n");

  printf("===== END STATE OF WORLD %s (gc: %d) =====\n", desc, gciteration);
}

void collectGarbage() {
  static int gcnestlevel = 0;
  int ret;

  (void)ret;

  cb_offset_t new_lower_bound = cb_region_cursor(&thread_region);

  if (pinned_lower_bound != CB_NULL && cb_offset_cmp(pinned_lower_bound, new_lower_bound) == -1)
    new_lower_bound = pinned_lower_bound;

  (void)gcnestlevel;

#ifdef DEBUG_TRACE_GC
  printf("-- gc begin nestlevel:%d, NEW_LOWER_BOUND:%ju\n", gcnestlevel++, (uintmax_t)new_lower_bound);
  size_t before = vm.bytesAllocated;
  printStateOfWorld("pre-gc");
#endif

  gc_phase = GC_PHASE_FREEZE_A_REGIONS;
  freezeARegions(new_lower_bound);

  gc_phase = GC_PHASE_RESET_GC_STATE;
  cb_rewind_to(gc_thread_cb, 0);
  printf("DANDEBUG before GC region allocation\n");
  ret = cb_region_create(&gc_thread_cb, &gc_thread_region, 1, 1024, 0);
  printf("DANDEBUG after GC region allocation\n");
  if (ret != CB_SUCCESS)
  {
      fprintf(stderr, "Could not create GC region.\n");
      assert(ret == CB_SUCCESS);
      exit(EXIT_FAILURE);  //FIXME _exit()?
  }
  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = CB_NULL;
  clearDarkObjectSet();

  //NOTE: The compilation often hold objects in stack-based (the C language
  // stack, not the vm.stack) temporaries which are invisble to the garbage
  // collector.  Finding all code paths where multiple allocations happen and
  // ensuring that the earlier allocations wind up temporarily on the vm.stack
  // so as not to be freed by the garbage collector is difficult.  This
  // alternative is to just consider all objtable objects as reachable until
  // we get out of the compilation phase.  The root_a layer is empty, so not
  // traversed here.
  if (exec_phase == EXEC_PHASE_COMPILE) {
    int ret;

    ret = cb_bst_traverse(thread_cb,
                          thread_objtable.root_b,
                          &grayObjtableTraversal,
                          (void*)"B");
    assert(ret == 0);
    ret = cb_bst_traverse(thread_cb,
                          thread_objtable.root_c,
                          &grayObjtableTraversal,
                          (void*)"C");
  }

  gc_phase = GC_PHASE_MARK_STACK_ROOTS;
  for (unsigned int i = 0; i < vm.tristack.stackDepth; ++i) {
    grayValue(*tristack_at(&(vm.tristack), i));
  }

  gc_phase = GC_PHASE_MARK_FRAMES_ROOTS;
  for (unsigned int i = 0; i < vm.triframes.frameCount; i++) {
    grayObject(triframes_at(&(vm.triframes), i)->closure.id());
  }

  gc_phase = GC_PHASE_MARK_OPEN_UPVALUES;
  for (OID<ObjUpvalue> upvalue = vm.openUpvalues;
       !upvalue.is_nil();
       upvalue = upvalue.clip()->next) {
    grayObject(upvalue.id());
  }

  //NOTE: vm.strings is omitted here because it only holds weak references.
  // These entries will be removed during consolidation if they were not
  // grayed as reachable from the root set.
  gc_phase = GC_PHASE_MARK_GLOBAL_ROOTS;
  grayTable(&vm.globals);
  grayCompilerRoots();
  grayObject(vm.initString.id());

  // Traverse the references.
  gc_phase = GC_PHASE_MARK_ALL_LEAVES;
  while (vm.grayCount > 0) {
    // Pop an item from the gray stack.
    OID<Obj> object = vm.grayStack.crp(gc_thread_cb)[--vm.grayCount];
    grayObjectLeaves(object);
  }

  collectGarbageCB(new_lower_bound);


  //Clobber old contents.
  //FIXME doing the clobber breaks things.
  if (false) {
    printf("DANDEBUG clobbering range [%ju,%ju) of ring (size: %ju, start: %ju, end: %ju)\n",
        (uintmax_t)cb_start(thread_cb),
        (uintmax_t)new_lower_bound,
        (uintmax_t)cb_ring_size(thread_cb),
        (uintmax_t)cb_start(thread_cb),
        (uintmax_t)cb_cursor(thread_cb));
    size_t clobber_len = new_lower_bound - cb_start(thread_cb);
    if (clobber_len > 0)
      cb_memset(thread_cb, cb_start(thread_cb), '@', clobber_len);
  } else {
    printf("DANDEBUG WOULD clobber range [%ju,%ju) of ring (size: %ju, start: %ju, end: %ju)\n",
        (uintmax_t)cb_start(thread_cb),
        (uintmax_t)new_lower_bound,
        (uintmax_t)cb_ring_size(thread_cb),
        (uintmax_t)cb_start(thread_cb),
        (uintmax_t)cb_cursor(thread_cb));
  }
  cb_start_advance(thread_cb, new_lower_bound - cb_start(thread_cb));

  // Adjust the heap size based on live memory.
  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

  gc_phase = GC_PHASE_NORMAL_EXEC;

#ifdef DEBUG_TRACE_GC
  printStateOfWorld("post-gc");
  printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld, nestlevel:%d\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC, --gcnestlevel);
  ++gciteration;
#endif
}

