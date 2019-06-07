#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "memory.h"
#include "vm.h"

#ifdef DEBUG_TRACE_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

ObjID reallocate(ObjID previous, size_t oldSize, size_t newSize, size_t alignment, bool suppress_gc) {
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
    objtable_invalidate(&thread_objtable, previous);
    return CB_NULL_OID;
  } else if (newSize < oldSize) {
#ifdef DEBUG_TRACE_GC
    // Clobber old values.
    cb_offset_t old_offset = objtable_lookup(&thread_objtable, previous);
    memset(((char *)cb_at(thread_cb, old_offset)) + newSize, '#', oldSize - newSize);
#endif
    return previous;
  } else {
    cb_offset_t old_offset = objtable_lookup(&thread_objtable, previous);
    cb_offset_t new_offset;
    int ret;

    ret = cb_region_memalign(&thread_cb,
                             &thread_region,
                             &new_offset,
                             alignment,
                             newSize);
    if (ret != CB_SUCCESS) {
      return CB_NULL_OID;
    }

    //Q: Should we keep the ObjID the same over reallocation?
    //A: No, changing it adheres to the earlier API which expects a shift of
    // offset (or earlier API than that, pointer).  Although it may work to
    // leave the ObjID the same, this may gloss over errors elsewhere, so we
    // force it to change for the sake of provoking any such errors.
    memcpy(cb_at(thread_cb, new_offset), cb_at(thread_cb, old_offset), oldSize);
#ifdef DEBUG_TRACE_GC
    // Clobber old values.
    memset(cb_at(thread_cb, old_offset), '!', oldSize);
#endif
    objtable_invalidate(&thread_objtable, previous);
    return objtable_add(&thread_objtable, new_offset);
  }
}

void grayObject(OID<Obj> objectOID) {
  if (objectOID.is_nil()) return;

  // Don't get caught in cycle.
  if (objectOID.lp()->isDark) return;

#ifdef DEBUG_TRACE_GC
  printf("#%ju grayObject() ", (uintmax_t)objectOID.id().id);
  printValue(OBJ_VAL(objectOID.id()));
  printf("\n");
#endif

  objectOID.lp()->isDark = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    int oldGrayCapacity = vm.grayCapacity;
    ObjID oldGrayStackID = vm.grayStack.id();
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);

    vm.grayStack = reallocate(oldGrayStackID,
                              sizeof(OID<Obj>) * oldGrayCapacity,
                              sizeof(OID<Obj>) * vm.grayCapacity,
                              cb_alignof(OID<Obj>),
                              true);

#ifdef DEBUG_TRACE_GC
    printf("#%ju OID<Obj>[%zd] array (%zd bytes) resized to #%ju OID<Obj>[%zd] array (%zd bytes)\n",
           (uintmax_t)oldGrayStackID.id,
           (size_t)oldGrayCapacity,
           sizeof(OID<Obj*>) * oldGrayCapacity,
           (uintmax_t)vm.grayStack.id().id,
           (size_t)vm.grayCapacity,
           sizeof(OID<Obj*>) * vm.grayCapacity);
#endif

  }

  vm.grayStack.lp()[vm.grayCount++] = objectOID;
}

void grayValue(Value value) {
  if (!IS_OBJ(value)) return;
  grayObject(AS_OBJ_ID(value));
}

static bool isWhiteObject(OID<Obj> objectOID) {
  if (objectOID.is_nil()) return true;
  if (objectOID.lp()->isDark) return false;
  return true;
}

bool isWhite(Value value) {
  if (!IS_OBJ(value)) return true;
  return isWhiteObject(AS_OBJ_ID(value));
}

static void grayArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    grayValue(array->values.lp()[i]);
  }
}

static void blackenObject(OID<Obj> objectOID) {
  Obj *object = objectOID.lp();

#ifdef DEBUG_TRACE_GC
  printf("#%ju blackenObject() ", (uintmax_t)objectOID.id().id);
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
        grayObject(closure->upvalues.lp()[i].id());
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

  switch (object.lp()->type) {
    case OBJ_BOUND_METHOD:
      FREE(ObjBoundMethod, object.id());
      break;

    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object.lp();
      freeTable(&klass->methods);
      FREE(ObjClass, object.id());
      break;
    }

    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object.lp();
      FREE_ARRAY(Value, closure->upvalues.id(), closure->upvalueCount);
      FREE(ObjClosure, object.id());
      break;
    }

    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object.lp();
      freeChunk(&function->chunk);
      FREE(ObjFunction, object.id());
      break;
    }

    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object.lp();
      freeTable(&instance->fields);
      FREE(ObjInstance, object.id());
      break;
    }

    case OBJ_NATIVE:
      FREE(ObjNative, object.id());
      break;

    case OBJ_STRING: {
      ObjString* string = (ObjString*)object.lp();
      FREE_ARRAY(char, string->chars.id(), string->length + 1);
      string->chars = CB_NULL_OID;
      FREE(ObjString, object.id());
      break;
    }

    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object.id());
      break;
  }
}

void collectGarbage() {
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
       upvalue = upvalue.lp()->next) {
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
    OID<Obj> object = vm.grayStack.lp()[--vm.grayCount];
    blackenObject(object);
  }

  // Delete unused interned strings.
  tableRemoveWhite(&vm.strings);

  // Collect the white objects.
  OID<Obj>* object = &vm.objects;
  while (! (*object).is_nil()) {
    if (!((*object).lp()->isDark)) {
      // This object wasn't reached, so remove it from the list and
      // free it.
      OID<Obj> unreached = (*object);
      //Obj* unreached = (*object).lp();
      //*object = unreached->next;
      *object = (*object).lp()->next;
      freeObject(unreached);
    } else {
      // This object was reached, so unmark it (for the next GC) and
      // move on to the next.
      (*object).lp()->isDark = false;
      object = &((*object).lp()->next);
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
    OID<Obj> next = object.lp()->next;
    freeObject(object);
    object = next;
  }

  ObjID oldGrayStackID = vm.grayStack.id();
  int oldGrayCapacity = vm.grayCapacity;
  vm.grayStack = reallocate(oldGrayStackID,
                            sizeof(OID<Obj>) * oldGrayCapacity,
                            0,
                            cb_alignof(OID<Obj>),
                            true);

#ifdef DEBUG_TRACE_GC
    printf("#%ju OID<Obj>[%zd] (grayStack) array freed (%zd bytes)\n",
           (uintmax_t)oldGrayStackID.id,
           (size_t)oldGrayCapacity,
           sizeof(OID<Obj*>) * oldGrayCapacity);
#endif
}
