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

cb_offset_t reallocate(cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment, bool suppress_gc) {
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

  if (newSize == 0) {
    return CB_NULL;
  } else if (newSize < oldSize) {
#ifdef DEBUG_TRACE_GC
    // Clobber old contents.
    memset(((char *)cb_at(thread_cb, previous)) + newSize, '#', oldSize - newSize);
#endif
    return previous;
  } else {
    cb_offset_t new_offset;
    int ret;

    ret = cb_region_memalign(&thread_cb,
                             &thread_region,
                             &new_offset,
                             alignment,
                             newSize);
    if (ret != CB_SUCCESS) {
      return CB_NULL;
    }

    //Q: Should we keep the ObjID the same over reallocation?
    //A: No, changing it adheres to the earlier API which expects a shift of
    // offset (or earlier API than that, pointer).  Although it may work to
    // leave the ObjID the same, this may gloss over errors elsewhere, so we
    // force it to change for the sake of provoking any such errors.
    memcpy(cb_at(thread_cb, new_offset), cb_at(thread_cb, previous), oldSize);
#ifdef DEBUG_TRACE_GC
    // Clobber old values.
    memset(cb_at(thread_cb, previous), '!', oldSize);
#endif
    return new_offset;
  }
}

void grayObject(OID<Obj> objectOID) {
  if (objectOID.is_nil()) return;

  // Don't get caught in cycle.
  if (objectOID.clip()->isDark) return;

#ifdef DEBUG_TRACE_GC
  printf("#%ju grayObject() ", (uintmax_t)objectOID.id().id);
  printValue(OBJ_VAL(objectOID.id()));
  printf("\n");
#endif

  objectOID.mlip()->isDark = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    int oldGrayCapacity = vm.grayCapacity;
    cb_offset_t oldGrayStackOffset = vm.grayStack.co();
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);

    vm.grayStack = reallocate(oldGrayStackOffset,
                              sizeof(OID<Obj>) * oldGrayCapacity,
                              sizeof(OID<Obj>) * vm.grayCapacity,
                              cb_alignof(OID<Obj>),
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
  if (objectOID.clip()->isDark) return false;
  return true;
}

bool isWhite(Value value) {
  if (!IS_OBJ(value)) return true;
  return isWhiteObject(AS_OBJ_ID(value));
}

static void grayArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    grayValue(array->values.clp()[i]);
  }
}

static void grayObjectLeaves(OID<Obj> objectOID) {
  Obj *object = objectOID.mlip();

#ifdef DEBUG_TRACE_GC
  printf("#%ju grayObjectLeaves() ", (uintmax_t)objectOID.id().id);
  printValue(OBJ_VAL(objectOID.id()));
  printf("\n");
#endif

  switch (object->type) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      grayValue(bound->receiver);
      grayObject(bound->method.id());
      break;
    }

    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      grayObject(klass->name.id());
      grayObject(klass->superclass.id());
      grayTable(&klass->methods);
      break;
    }

    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      grayObject(closure->function.id());
      for (int i = 0; i < closure->upvalueCount; i++) {
        grayObject(closure->upvalues.clp()[i].id());
      }
      break;
    }

    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      grayObject(function->name.id());
      grayArray(&function->chunk.constants);
      break;
    }

    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      grayObject(instance->klass.id());
      grayTable(&instance->fields);
      break;
    }

    case OBJ_UPVALUE:
      grayValue(((ObjUpvalue*)object)->closed);
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

  switch (object.clip()->type) {
    case OBJ_BOUND_METHOD:
      FREE(ObjBoundMethod, object.co());
      break;

    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object.clip();
      freeTable(&klass->methods);
      FREE(ObjClass, object.co());
      break;
    }

    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object.clip();
      FREE_ARRAY(Value, closure->upvalues.co(), closure->upvalueCount);
      FREE(ObjClosure, object.co());
      break;
    }

    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object.clip();
      freeChunk(&function->chunk);
      FREE(ObjFunction, object.co());
      break;
    }

    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object.clip();
      freeTable(&instance->fields);
      FREE(ObjInstance, object.co());
      break;
    }

    case OBJ_NATIVE:
      FREE(ObjNative, object.co());
      break;

    case OBJ_STRING: {
      ObjString* string = (ObjString*)object.clip();
      FREE_ARRAY(char, string->chars.co(), string->length + 1);
      string->chars = CB_NULL;
      FREE(ObjString, object.co());
      break;
    }

    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object.co());
      break;
  }

  objtable_invalidate(&thread_objtable, object.id());
}

void collectGarbageCB() {
#ifdef DEBUG_TRACE_GC
  printf("-- BEGIN CB GC\n");
#endif

  struct gc_request req;
  struct gc_response resp;
  int ret;

  memset(&req, 0, sizeof(req));
  memset(&resp, 0, sizeof(resp));

  //FIXME prepare request contents
  req.orig_cb = thread_cb;
  size_t objtable_b_size = cb_bst_size(thread_cb, thread_objtable.root_b);
  size_t objtable_c_size = cb_bst_size(thread_cb, thread_objtable.root_c);
  printf("DANDEBUG objtable_b_size: %zd, objtable_c_size: %zd\n",
         objtable_b_size, objtable_c_size);
  ret = cb_region_create(&thread_cb,
                         &req.objtable_new_region,
                         64 /* FIXME cacheline size */,
                         objtable_b_size + objtable_c_size,
                         CB_REGION_FINAL); //FIXME CB_REGION_REVERSED  brken here
  printf("DANDEBUG cb_region_create(): %d\n", ret);
  assert(ret == 0);
  req.objtable_root_b = thread_objtable.root_b;
  req.objtable_root_c = thread_objtable.root_c;

  ret = gc_perform(&req, &resp);
  if (ret != 0) {
    fprintf(stderr, "Failed to GC via CB.\n");
  }
  assert(ret == 0);

  //FIXME integrate response contents
  printf("DANDEBUG objtable C %ju -> %ju\n", (uintmax_t)thread_objtable.root_c, (uintmax_t)resp.objtable_new_root_c);
  thread_objtable.root_c = resp.objtable_new_root_c;
  printf("DANDEBUG objtable B %ju -> %ju\n", (uintmax_t)thread_objtable.root_b, (uintmax_t)thread_objtable.root_a);
  thread_objtable.root_b = thread_objtable.root_a;
  printf("DANDEBUG objtable A %ju -> %ju\n", (uintmax_t)thread_objtable.root_a, (uintmax_t)CB_BST_SENTINEL);
  thread_objtable.root_a = CB_BST_SENTINEL;

#ifdef DEBUG_TRACE_GC
  printf("-- END CB GC\n");
#endif
}

void collectGarbage() {
  collectGarbageCB();
#ifdef DEBUG_TRACE_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

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
  grayInterningTable(&vm.strings);
  grayTable(&vm.globals);
  grayCompilerRoots();
  grayObject(vm.initString.id());

  // Traverse the references.
  while (vm.grayCount > 0) {
    // Pop an item from the gray stack.
    OID<Obj> object = vm.grayStack.clp()[--vm.grayCount];
    grayObjectLeaves(object);
  }

  // Delete unused interned strings.
  tableRemoveWhite(&vm.strings);

  // Collect the white objects.
  OID<Obj>* object = &vm.objects;
  while (! (*object).is_nil()) {
    if (!((*object).clip()->isDark)) {
      // This object wasn't reached, so remove it from the list and
      // free it.
      OID<Obj> unreached = (*object);
      //Obj* unreached = (*object).lp();
      //*object = unreached->next;
      *object = (*object).clip()->next;
      freeObject(unreached);
    } else {
      // This object was reached, so unmark it (for the next GC) and
      // move on to the next.
      (*object).mlip()->isDark = false;
      object = &((*object).mlip()->next);
    }
  }

  // Adjust the heap size based on live memory.
  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_TRACE_GC
  printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC);
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
                            true);

#ifdef DEBUG_TRACE_GC
    printf("@%ju OID<Obj>[%zd] (grayStack) array freed (%zd bytes)\n",
           (uintmax_t)oldGrayStackOffset,
           (size_t)oldGrayCapacity,
           sizeof(OID<Obj*>) * oldGrayCapacity);
#endif
}
