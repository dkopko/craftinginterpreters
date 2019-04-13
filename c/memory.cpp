//> Chunks of Bytecode memory-c
#include <stdlib.h>

#include "common.h"
//> Garbage Collection not-yet
#include "compiler.h"
//< Garbage Collection not-yet
#include "memory.h"
//> Strings memory-include-vm
#include "vm.h"
//< Strings memory-include-vm
//> Garbage Collection not-yet

#ifdef DEBUG_TRACE_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2
//< Garbage Collection not-yet

cb_offset_t reallocate(cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment) {
//> Garbage Collection not-yet
  vm.bytesAllocated += newSize - oldSize;

  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
  }

//< Garbage Collection not-yet
  if (newSize == 0) {
    return (cb_offset_t)0;
  } else if (newSize < oldSize) {
    return previous;
  } else {
    int ret;
    cb_offset_t new_offset;

    ret = cb_memalign(&thread_cb, &new_offset, alignment, newSize);
    if (ret != CB_SUCCESS) {
      return (cb_offset_t)0;
    }
    memcpy(cb_at(thread_cb, new_offset), cb_at(thread_cb, previous), oldSize);
    return new_offset;
  }
}

//> Garbage Collection not-yet

void grayObject(Obj* object) {
  return; //CBINT

  if (object == NULL) return;

  // Don't get caught in cycle.
  if (object->isDark) return;

#ifdef DEBUG_TRACE_GC
  printf("%p gray ", object);
  printValue(OBJ_VAL(object));
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

void grayValue(Value value) {
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
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  switch (object->type) {
//> Methods and Initializers not-yet
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      grayValue(bound->receiver);
      grayObject((Obj*)bound->method.lp());
      break;
    }
//< Methods and Initializers not-yet
//> Classes and Instances not-yet

    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      grayObject((Obj*)klass->name.lp());
//> Superclasses not-yet
      grayObject((Obj*)klass->superclass.lp());
//< Superclasses not-yet
//> Methods and Initializers not-yet
      grayTable(&klass->methods);
//< Methods and Initializers not-yet
      break;
    }

//< Classes and Instances not-yet
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

//> Classes and Instances not-yet
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      grayObject((Obj*)instance->klass.lp());
      grayTable(&instance->fields);
      break;
    }

//< Classes and Instances not-yet
    case OBJ_UPVALUE:
      grayValue(((ObjUpvalue*)object)->closed);
      break;

    case OBJ_NATIVE:
    case OBJ_STRING:
      // No references.
      break;
  }
}
//< Garbage Collection not-yet
//> Strings free-object
static void freeObject(Obj* object) {
}

#if 0
static void freeObject(Obj* object) {
//> Garbage Collection not-yet
#ifdef DEBUG_TRACE_GC
  printf("%p free ", object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

//< Garbage Collection not-yet
  switch (object->type) {
//> Methods and Initializers not-yet
    case OBJ_BOUND_METHOD:
      FREE(ObjBoundMethod, object);
      break;

//< Methods and Initializers not-yet
/* Classes and Instances not-yet < Methods and Initializers not-yet
    case OBJ_CLASS:
*/
//> Classes and Instances not-yet
//> Methods and Initializers not-yet
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      freeTable(&klass->methods);
//< Methods and Initializers not-yet
      FREE(ObjClass, object);
      break;
//> Methods and Initializers not-yet
    }
//< Methods and Initializers not-yet

//< Classes and Instances not-yet
//> Closures not-yet
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(Value, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }

//< Closures not-yet
//> Calls and Functions not-yet
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }

//< Calls and Functions not-yet
//> Classes and Instances not-yet
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      freeTable(&instance->fields);
      FREE(ObjInstance, object);
      break;
    }

//< Classes and Instances not-yet
//> Calls and Functions not-yet
    case OBJ_NATIVE:
      FREE(ObjNative, object);
      break;

//< Calls and Functions not-yet
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      string->chars = NULL;
      FREE(ObjString, object);
      break;
    }
//> Closures not-yet

    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object);
      break;
//< Closures not-yet
  }
}
#endif
//< Strings free-object
//> Garbage Collection not-yet

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

  for (int i = 0; i < vm.frameCount; i++) {
    grayObject((Obj*)vm.frames[i].closure.lp());
  }

  // Mark the open upvalues.
  for (CBO<ObjUpvalue> upvalue = vm.openUpvalues;
       !upvalue.is_nil();
       upvalue = upvalue.lp()->next) {
    grayObject((Obj*)upvalue.lp());
  }

  // Mark the global roots.
  grayTable(&vm.globals);
  grayCompilerRoots();
//> Methods and Initializers not-yet
  grayObject((Obj*)vm.initString.lp());
//< Methods and Initializers not-yet

  // Traverse the references.
  while (vm.grayCount > 0) {
    // Pop an item from the gray stack.
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }

  // Delete unused interned strings.
  tableRemoveWhite(&vm.strings);

  // Collect the white objects.
  Obj** object = &vm.objects;
  while (*object != NULL) {
    if (!((*object)->isDark)) {
      // This object wasn't reached, so remove it from the list and
      // free it.
      Obj* unreached = *object;
      *object = unreached->next;
      freeObject(unreached);
    } else {
      // This object was reached, so unmark it (for the next GC) and
      // move on to the next.
      (*object)->isDark = false;
      object = &(*object)->next;
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
//< Garbage Collection not-yet
//> Strings free-objects
void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
//> Garbage Collection not-yet

  free(vm.grayStack);
//< Garbage Collection not-yet
}
//< Strings free-objects
