#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cb_bst.h>
#include <cb_region.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_TRACE_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

int gc_phase = GC_PHASE_NORMAL_EXEC;


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

cb_offset_t reallocate(cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment, bool isObject, bool suppress_gc) {
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
    char *mem = (char *)cb_at(thread_cb, previous);
    assert(alloc_size_get(mem) == oldSize);
    assert(alloc_alignment_get(mem) == alignment);
    assert(alloc_is_object_get(mem) == isObject);
    (void)mem;
  }
#endif

  if (newSize == 0) {
    clobber_mem(cb_at(thread_cb, previous), oldSize);
    return CB_NULL;
  } else if (newSize < oldSize) {
    clobber_mem(((char *)cb_at(thread_cb, previous)) + newSize, oldSize - newSize);
    return previous;
  } else {
    size_t header_size = sizeof(size_t)   /* size field */
                         + sizeof(size_t) /* alignment field */
                         + sizeof(bool);  /* isObject field */
    size_t needed_contiguous_size = header_size + (alignment - 1) + newSize;
    cb_offset_t new_offset;
    int ret;

    ret = cb_region_memalign(&thread_cb,
                             &thread_region,
                             &new_offset,
                             alignment,
                             needed_contiguous_size);
    if (ret != CB_SUCCESS) {
      return CB_NULL;
    }
    new_offset = cb_offset_aligned_gte(new_offset + header_size, alignment);

    char *mem = (char *)cb_at(thread_cb, new_offset);
    alloc_size_set(mem, newSize);
    alloc_alignment_set(mem, alignment);
    alloc_is_object_set(mem, isObject);

    //Q: Should we keep the ObjID the same over reallocation?
    //A: No, changing it adheres to the earlier API which expects a shift of
    // offset (or earlier API than that, pointer).  Although it may work to
    // leave the ObjID the same, this may gloss over errors elsewhere, so we
    // force it to change for the sake of provoking any such errors.
    if (previous != CB_NULL) {
      char *prevMem = (char *)cb_at(thread_cb, previous);
      memcpy(mem, prevMem, oldSize);
      clobber_mem(prevMem, oldSize);
    }

    return new_offset;
  }
}

bool objectIsDark(const OID<Obj> objectOID) {
  cb_term key_term;

  cb_term_set_u64(&key_term, objectOID.id().id);

  return cb_bst_contains_key(thread_cb,
                             thread_darkset_bst,
                             &key_term);
}

static void objectSetDark(OID<Obj> objectOID) {
  cb_term key_term;
  cb_term value_term;
  int ret;

  cb_term_set_u64(&key_term, objectOID.id().id);
  cb_term_set_u64(&value_term, objectOID.id().id);

  ret = cb_bst_insert(&thread_cb,  //FIXME CBINT gc_thread_cb
                      &thread_region, //FIXME CBINT gc_thread_region
                      &thread_darkset_bst,  //FIXME CBINT gc_thread_darkset_bst
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  assert(ret == 0);
  (void)ret;
}

static void clearDarkObjectSet(void) {
  thread_darkset_bst = CB_BST_SENTINEL;
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

    vm.grayStack = reallocate(oldGrayStackOffset,
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

  vm.grayStack.mlp()[vm.grayCount++] = objectOID;
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

#if 0
  const Obj   *obj        = object.clip();
  cb_offset_t  obj_offset = object.co();
  switch (obj->type) {
    case OBJ_BOUND_METHOD:
      FREE(ObjBoundMethod, obj_offset);
      break;

    case OBJ_CLASS: {
      //ObjClass* klass = (ObjClass*)obj;
      //freeTable(&klass->methods); FIXME CBINT
      FREE(ObjClass, obj_offset);
      break;
    }

    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)obj;
      FREE_ARRAY(Value, closure->upvalues.co(), closure->upvalueCount);
      FREE(ObjClosure, obj_offset);
      break;
    }

    case OBJ_FUNCTION: {
      //ObjFunction* function = (ObjFunction*)obj;
      freeChunk(object.id());
      FREE(ObjFunction, obj_offset);
      break;
    }

    case OBJ_INSTANCE: {
      //ObjInstance* instance = (ObjInstance*)obj;
      // freeTable(&instance->fields); FIXME CBINT
      FREE(ObjInstance, obj_offset);
      break;
    }

    case OBJ_NATIVE:
      FREE(ObjNative, obj_offset);
      break;

    case OBJ_STRING: {
      ObjString* string = (ObjString*)obj;
      FREE_ARRAY(char, string->chars.co(), string->length + 1);
      string->chars = CB_NULL;
      FREE(ObjString, obj_offset);
      break;
    }

    case OBJ_UPVALUE:
      FREE(ObjUpvalue, obj_offset);
      break;
  }
#endif
}

cb_offset_t deriveMutableObjectLayer(ObjID id, cb_offset_t object_offset) {
  CBO<Obj> srcCBO = object_offset;
  CBO<Obj> destCBO;

  printf("#%ju@%ju deriveMutableObjectLayer() ", (uintmax_t)id.id, object_offset);
  printObject(id, object_offset, srcCBO.clp());
  printf("\n");

  switch (srcCBO.clp()->type) {
    case OBJ_BOUND_METHOD: {
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjBoundMethod), cb_alignof(ObjBoundMethod), true, true);
      ObjBoundMethod       *dest = (ObjBoundMethod *)destCBO.mlp();
      const ObjBoundMethod *src  = (const ObjBoundMethod *)srcCBO.clp();

      dest->obj      = src->obj;
      dest->receiver = src->receiver;
      dest->method   = src->method;

      break;
    }

    case OBJ_CLASS: {
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjClass), cb_alignof(ObjClass), true, true);
      ObjClass       *dest = (ObjClass *)destCBO.mlp();
      const ObjClass *src  = (const ObjClass *)srcCBO.clp();

      dest->obj         = src->obj;
      dest->name        = src->name;
      dest->superclass  = src->superclass;
      //NOTE: We expect lookup of methods to first check this new, mutable,
      //  A-region ObjClass, before looking at older versions in B and C.
      methods_layer_init(&(dest->methods_bst));
      break;
    }

    case OBJ_CLOSURE: {
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjClosure), cb_alignof(ObjClosure), true, true);
      ObjClosure *dest      = (ObjClosure *)destCBO.mlp();
      const ObjClosure *src = (const ObjClosure *)srcCBO.clp();

      dest->obj          = src->obj;
      dest->function     = src->function;
      dest->upvalues     = GROW_ARRAY_NOGC(CB_NULL, OID<ObjUpvalue>, 0, src->upvalueCount);
      {
        const OID<ObjUpvalue> *srcUpvalues = src->upvalues.clp();
        OID<ObjUpvalue> *destUpvalues = dest->upvalues.mlp();

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
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjFunction), cb_alignof(ObjFunction), true, true);
      ObjFunction       *dest = (ObjFunction *)destCBO.mlp();
      const ObjFunction *src  = (const ObjFunction *)srcCBO.clp();

      dest->obj          = src->obj;
      dest->arity        = src->arity;
      dest->upvalueCount = src->upvalueCount;
      dest->chunk.count    = src->chunk.count;
      dest->chunk.capacity = src->chunk.capacity;
      dest->chunk.code = GROW_ARRAY_NOGC(CB_NULL, uint8_t, 0, src->chunk.capacity);
      memcpy(dest->chunk.code.mlp(), src->chunk.code.clp(), src->chunk.capacity * sizeof(uint8_t));
      dest->chunk.lines = GROW_ARRAY_NOGC(CB_NULL, int, 0, src->chunk.capacity);
      memcpy(dest->chunk.lines.mlp(), src->chunk.lines.clp(), src->chunk.capacity * sizeof(int));
      dest->chunk.constants.capacity = src->chunk.constants.capacity;
      dest->chunk.constants.count    = src->chunk.constants.count;
      dest->chunk.constants.values   = GROW_ARRAY_NOGC(CB_NULL, Value, 0, src->chunk.constants.capacity);
      memcpy(dest->chunk.constants.values.mlp(), src->chunk.constants.values.clp(), src->chunk.constants.capacity * sizeof(Value));
      dest->name         = src->name;

      break;
    }

    case OBJ_INSTANCE: {
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjInstance), cb_alignof(ObjInstance), true, true);
      ObjInstance       *dest = (ObjInstance *)destCBO.mlp();
      const ObjInstance *src  = (const ObjInstance *)srcCBO.clp();

      dest->obj        = src->obj;
      dest->klass      = src->klass;
      //NOTE: We expect lookup of fields to first check this new, mutable,
      //  A-region ObjClass, before looking at older versions in B and C.
      fields_layer_init(&(dest->fields_bst));
      break;
    }

    case OBJ_NATIVE: {
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjNative), cb_alignof(ObjNative), true, true);
      ObjNative       *dest = (ObjNative *)destCBO.mlp();
      const ObjNative *src  = (const ObjNative *)srcCBO.clp();

      dest->obj      = src->obj;
      dest->function = src->function;

      break;
    }

    case OBJ_STRING: {
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjString), cb_alignof(ObjString), true, true);
      ObjString       *dest = (ObjString *)destCBO.mlp();
      const ObjString *src  = (const ObjString *)srcCBO.clp();

      dest->obj    = src->obj;
      dest->length = src->length;
      dest->chars  = GROW_ARRAY_NOGC(CB_NULL, char, 0, src->length + 1);
      memcpy(dest->chars.mlp(), src->chars.clp(), src->length * sizeof(char));
      dest->chars.mlp()[src->length] = '\0';
      dest->hash   = src->hash;

      break;
    }

    case OBJ_UPVALUE: {
      destCBO = reallocate(CB_NULL, 0, sizeof(ObjUpvalue), cb_alignof(ObjUpvalue), true, true);
      ObjUpvalue       *dest = (ObjUpvalue *)destCBO.mlp();
      const ObjUpvalue *src  = (const ObjUpvalue *)srcCBO.clp();

      dest->obj             = src->obj;
      dest->valueStackIndex = src->valueStackIndex;
      dest->closed          = src->closed;
      dest->next            = src->next;

      break;
    }
  }

  return destCBO.mo();
}

static int
copy_entry_to_bst(const struct cb_term *key_term,
                  const struct cb_term *value_term,
                  void                 *closure)
{
  cb_offset_t *dest_bst = (cb_offset_t *)closure;
  int ret;

  (void)ret;

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      dest_bst,
                      cb_region_start(&thread_region),  //NOTE: full contents are mutable
                      key_term,
                      value_term);
  assert(ret == 0);

  return 0;
}

cb_offset_t cloneObject(ObjID id, cb_offset_t object_offset) {
  CBO<Obj> srcCBO = object_offset;
  CBO<Obj> cloneCBO = deriveMutableObjectLayer(id, object_offset);
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
      ObjClass *srcClass = (ObjClass *)srcCBO.clp();
      ObjClass *destClass = (ObjClass *)cloneCBO.clp();

      ret = cb_bst_traverse(thread_cb,
                            srcClass->methods_bst,
                            copy_entry_to_bst,
                            &(destClass->methods_bst));
      assert(ret == 0);
    }
    break;

    case OBJ_INSTANCE: {
      ObjInstance *srcInstance = (ObjInstance *)srcCBO.clp();
      ObjInstance *destInstance = (ObjInstance *)cloneCBO.clp();

      ret = cb_bst_traverse(thread_cb,
                            srcInstance->fields_bst,
                            copy_entry_to_bst,
                            &(destInstance->fields_bst));
      assert(ret == 0);
    }
    break;

    default:
      break;
  }

  return cloneCBO.mo();
}

void freezeARegions() {
  int ret;

  // === Begin Freeze A regions ===
  // Objtable
  assert(cb_bst_num_entries(thread_cb, thread_objtable.root_c) == 0);
  thread_objtable.root_c = thread_objtable.root_b;
  thread_objtable.root_b = thread_objtable.root_a;
  ret = objtable_layer_init(&(thread_objtable.root_a));
  assert(ret == 0);

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

  gc_phase = GC_PHASE_ACTIVE_GC;

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
  // === End Freeze A regions ===

}

void collectGarbageCB() {
  static int gccount = 0;
  struct gc_request req;
  struct gc_response resp;
  int ret;

  (void)gccount;
#ifdef DEBUG_TRACE_GC
  printf("-- BEGIN CB GC %d\n", gccount);
#endif

  memset(&req, 0, sizeof(req));
  memset(&resp, 0, sizeof(resp));

  //FIXME prepare request contents
  req.orig_cb = thread_cb;

  // Prepare condensing objtable B+C
  size_t objtable_b_size = cb_bst_size(thread_cb, thread_objtable.root_b);
  size_t objtable_c_size = cb_bst_size(thread_cb, thread_objtable.root_c);
  printf("DANDEBUG objtable_b_size: %zd, objtable_c_size: %zd\n",
         objtable_b_size, objtable_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.objtable_new_region,
                         64 /* FIXME cacheline size */,
                         objtable_b_size + objtable_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  req.objtable_root_b = thread_objtable.root_b;
  req.objtable_root_c = thread_objtable.root_c;

  // Prepare condensing tristack B+C
  size_t tristack_b_plus_c_size = sizeof(Value) * (vm.tristack.abi - vm.tristack.cbi);
  printf("DANDEBUG tristack_b_plus_c_size: %zd\n", tristack_b_plus_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.tristack_new_region,
                         64 /* FIXME cacheline size */,
                         tristack_b_plus_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
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
                         64 /* FIXME cacheline size */,
                         triframes_b_plus_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  req.triframes_abi        = vm.triframes.abi;
  req.triframes_bbo        = vm.triframes.bbo;
  req.triframes_bbi        = vm.triframes.bbi;
  req.triframes_cbo        = vm.triframes.cbo;
  req.triframes_cbi        = vm.triframes.cbi;
  req.triframes_frameCount = vm.triframes.frameCount;


  // Prepare condensing strings B+C
  //FIXME should these be calls to cb_bst_internal_size() instead?
  size_t strings_b_size = cb_bst_size(thread_cb, vm.strings.root_b);
  size_t strings_c_size = cb_bst_size(thread_cb, vm.strings.root_c);
  printf("DANDEBUG strings_b_size: %zd, strings_c_size: %zd\n",
         strings_b_size, strings_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.strings_new_region,
                         64 /* FIXME cacheline size */,
                         strings_b_size + strings_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  req.strings_root_b = vm.strings.root_b;
  req.strings_root_c = vm.strings.root_c;

  // Prepare condensing globals B+C
  //FIXME should these be calls to cb_bst_internal_size() instead?
  size_t globals_b_size = cb_bst_size(thread_cb, vm.globals.root_b);
  size_t globals_c_size = cb_bst_size(thread_cb, vm.globals.root_c);
  printf("DANDEBUG globals_b_size: %zd, globals_c_size: %zd\n",
         globals_b_size, globals_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.globals_new_region,
                         64 /* FIXME cacheline size */,
                         globals_b_size + globals_c_size,
                         CB_REGION_FINAL);
  assert(ret == 0);
  req.globals_root_b = vm.globals.root_b;
  req.globals_root_c = vm.globals.root_c;


  //Do the Garbage Collection / Condensing.
  ret = gc_perform(&req, &resp);
  if (ret != 0) {
    fprintf(stderr, "Failed to GC via CB.\n");
  }
  assert(ret == 0);


  //Integrate condensed objtable.
  cb_offset_t old_objtable_root_c = thread_objtable.root_c;
  ret = objtable_layer_init(&(thread_objtable.root_c));
  assert(ret == 0);
  printf("DANDEBUG objtable C %ju -> %ju\n", (uintmax_t)old_objtable_root_c, (uintmax_t)thread_objtable.root_c);
  printf("DANDEBUG objtable B %ju -> %ju\n", (uintmax_t)thread_objtable.root_b, (uintmax_t)resp.objtable_new_root_b);
  thread_objtable.root_b = resp.objtable_new_root_b;

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
  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(vm.strings.root_c),
                    &clox_value_deep_comparator,
                    &clox_value_deep_comparator,
                    &clox_value_render,
                    &clox_value_render,
                    &clox_no_external_size,
                    &clox_no_external_size);
  assert(ret == 0);
  vm.strings.root_b = resp.strings_new_root_b;

  //Integrate condensed globals.
  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(vm.globals.root_c),
                    &clox_value_deep_comparator,
                    &clox_value_deep_comparator,
                    &clox_value_render,
                    &clox_value_render,
                    &clox_no_external_size,
                    &clox_no_external_size);
  assert(ret == 0);
  vm.globals.root_b = resp.globals_new_root_b;

  if (vm.currentFrame)
    vm.currentFrame->slots = tristack_at(&(vm.tristack), vm.currentFrame->slotsIndex);

  gc_phase = GC_PHASE_NORMAL_EXEC;

#ifdef DEBUG_TRACE_GC
  printf("-- END CB GC %d\n", gccount++);
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

void printStateOfWorld(const char *desc) {
  int ret;

  (void)ret;

  printf("===== BEGIN STATE OF WORLD %s =====\n", desc);

  printf("----- begin objtable -----\n");
  ret = cb_bst_traverse(thread_cb,
                        thread_objtable.root_a,
                        &printObjtableTraversal,
                        (void*)"A");
  ret = cb_bst_traverse(thread_cb,
                        thread_objtable.root_b,
                        &printObjtableTraversal,
                        (void*)"B");
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
  {
    OID<ObjUpvalue> it = vm.openUpvalues;
    printObject(it.id(), it.co(), (const Obj *)it.clip());
    printf("\n");
    it = it.clip()->next;
  }
  printf("----- end vm.openUpvalues -----\n");

  printf("===== END STATE OF WORLD %s =====\n", desc);
}

void collectGarbage() {
  static int gcnestlevel = 0;

  (void)gcnestlevel;

#ifdef DEBUG_TRACE_GC
  printf("-- gc begin nestlevel:%d\n", gcnestlevel++);
  size_t before = vm.bytesAllocated;
  printStateOfWorld("pre-gc");
#endif

  freezeARegions();

  // Mark the stack roots.
  for (unsigned int i = 0; i < vm.tristack.stackDepth; ++i) {
    grayValue(*tristack_at(&(vm.tristack), i));
  }

  for (unsigned int i = 0; i < vm.triframes.frameCount; i++) {
    grayObject(triframes_at(&(vm.triframes), i)->closure.id());
  }

  // Mark the open upvalues.
  for (OID<ObjUpvalue> upvalue = vm.openUpvalues;
       !upvalue.is_nil();
       upvalue = upvalue.clip()->next) {
    grayObject(upvalue.id());
  }

  // Mark the global roots.
  //NOTE: vm.strings is omitted here because it only holds weak references.
  // These entries will be removed during consolidation if they were not
  // grayed as reachable from the root set.
  grayTable(&vm.globals);
  grayCompilerRoots();
  grayObject(vm.initString.id());

  // Traverse the references.
  while (vm.grayCount > 0) {
    // Pop an item from the gray stack.
    OID<Obj> object = vm.grayStack.clp()[--vm.grayCount];
    grayObjectLeaves(object);
  }

  collectGarbageCB();

  // Collect the white objects.
  OID<Obj>* object = &vm.objects;
  while (! (*object).is_nil()) {
    if (!objectIsDark(*object)) {
      // This object wasn't reached, so remove it from the list and
      // free it.
      OID<Obj> unreached = (*object);
      *object = (*object).clip()->next;
      freeObject(unreached);
    } else {
      // This object was reached, so move on to the next.
      object = (OID<Obj>*)&((*object).clip()->next);  //FIXME CBINT const-casting here
    }
  }

  clearDarkObjectSet();

  // Adjust the heap size based on live memory.
  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_TRACE_GC
  printStateOfWorld("post-gc");
  printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld, nestlevel:%d\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC, --gcnestlevel);
#endif
}

void freeObjects() {
  OID<Obj> object = vm.objects;
  while (! object.is_nil()) {
    OID<Obj> next = object.clip()->next;
    freeObject(object);
    object = next;
  }

  cb_offset_t oldGrayStackOffset = vm.grayStack.co();
  int oldGrayCapacity = vm.grayCapacity;
  vm.grayStack = reallocate(oldGrayStackOffset,
                            sizeof(OID<Obj>) * oldGrayCapacity,
                            0,
                            cb_alignof(OID<Obj>),
                            false,
                            true);

#ifdef DEBUG_TRACE_GC
    printf("@%ju OID<Obj>[%zd] (grayStack) array freed (%zd bytes)\n",
           (uintmax_t)oldGrayStackOffset,
           (size_t)oldGrayCapacity,
           sizeof(OID<Obj*>) * oldGrayCapacity);
#endif
}
