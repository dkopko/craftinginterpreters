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

ObjID reallocate(ObjID previous, size_t oldSize, size_t newSize, size_t alignment) {
  vm.bytesAllocated += newSize - oldSize;

  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
  }

  if (newSize == 0) {
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

void grayObject(Obj* object) {
  return; //CBINT

  if (object == NULL) return;

  // Don't get caught in cycle.
  if (object->isDark) return;

#ifdef DEBUG_TRACE_GC
  printf("%p gray ", object);
  //printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isDark = true;

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);

    // Not using reallocate() here because we don't want to trigger the
    // GC inside a GC!
    vm.grayStack = (Obj **)realloc(vm.grayStack,
                           sizeof(Obj*) * vm.grayCapacity);
  }

  vm.grayStack[vm.grayCount++] = object;
}

static void grayValue(Value value) {
  if (!IS_OBJ(value)) return;
  grayObject(AS_OBJ(value));
}

static void grayArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    grayValue(array->values.lp()[i]);
  }
}

static void blackenObject(Obj* object) {
  return;  //CBINT

#ifdef DEBUG_TRACE_GC
  printf("%p blacken ", object);
  //printValue(OBJ_VAL(object));
  printf("\n");
#endif

  switch (object->type) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      grayValue(bound->receiver);
      grayObject((Obj*)bound->method.lp());
      break;
    }

    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      grayObject((Obj*)klass->name.lp());
      grayObject((Obj*)klass->superclass.lp());
      grayTable(&klass->methods);
      break;
    }

    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      grayObject((Obj*)closure->function.lp());
      for (int i = 0; i < closure->upvalueCount; i++) {
        grayObject((Obj*)closure->upvalues.lp()[i].lp());
      }
      break;
    }

    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      grayObject((Obj*)function->name.lp());
      grayArray(&function->chunk.constants);
      break;
    }

    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      grayObject((Obj*)instance->klass.lp());
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
  printf("%ju free ", (uintmax_t)object.id().id);
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
  //CBINT FIXME
  //for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
  //  grayValue(*slot);
  //}

  //CBINT FIXME
  //for (int i = 0; i < vm.frameCount; i++) {
  //  grayObject((Obj*)vm.frames[i].closure.lp());
  //}

  // Mark the open upvalues.
  for (OID<ObjUpvalue> upvalue = vm.openUpvalues;
       !upvalue.is_nil();
       upvalue = upvalue.lp()->next) {
    grayObject((Obj*)upvalue.lp());
  }

  // Mark the global roots.
  grayTable(&vm.globals);
  grayCompilerRoots();
  grayObject((Obj*)vm.initString.lp());

  // Traverse the references.
  while (vm.grayCount > 0) {
    // Pop an item from the gray stack.
    Obj* object = vm.grayStack[--vm.grayCount];
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

  free(vm.grayStack);
}
