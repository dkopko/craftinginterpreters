//> Strings object-c
#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
//> Hash Tables object-include-table
#include "table.h"
//< Hash Tables object-include-table
#include "value.h"
#include "vm.h"
//> allocate-obj

#define ALLOCATE_OBJ(type, objectType) \
    allocateObject(sizeof(type), cb_alignof(type), objectType)
//< allocate-obj
//> allocate-object

static ObjID allocateObject(size_t size, size_t alignment, ObjType type) {
  OID<Obj> objectOID = reallocate(CB_NULL_OID, 0, size, alignment);
  Obj* object = objectOID.lp();
  object->type = type;
//> Garbage Collection not-yet
  object->isDark = false;
//< Garbage Collection not-yet
//> add-to-list
  
  object->next = vm.objects;
  vm.objects = object;
//< add-to-list
//> Garbage Collection not-yet

#ifdef DEBUG_TRACE_GC
  //printf("%p allocate %ld for %d\n", object, size, type);
  printf("@%ju object was allocated (%ld bytes for type %d)\n", (uintmax_t)objectOID.id().id, size, type);
#endif

//< Garbage Collection not-yet
  return objectOID.id();
}
//< allocate-object
//> Methods and Initializers not-yet

OID<ObjBoundMethod> newBoundMethod(Value receiver, OID<ObjClosure> method) {
  OID<ObjBoundMethod> boundOID = ALLOCATE_OBJ(ObjBoundMethod,
                                       OBJ_BOUND_METHOD);
  ObjBoundMethod* bound = boundOID.lp();

  bound->receiver = receiver;
  bound->method = method;
  return boundOID.id();
}
//< Methods and Initializers not-yet
//> Classes and Instances not-yet

/* Classes and Instances not-yet < Superclasses not-yet
ObjClass* newClass(ObjString* name) {
*/
//> Superclasses not-yet
OID<ObjClass> newClass(OID<ObjString> name, OID<ObjClass> superclass) {
//< Superclasses not-yet
  OID<ObjClass> klassOID = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
  ObjClass* klass = klassOID.lp();
  klass->name = name;
//> Superclasses not-yet
  klass->superclass = superclass;
//< Superclasses not-yet
//> Methods and Initializers not-yet
  initTable(&klass->methods, &clox_value_shallow_comparator, &clox_value_render);
//< Methods and Initializers not-yet
  return klassOID;
}
//< Classes and Instances not-yet
//> Closures not-yet

OID<ObjClosure> newClosure(OID<ObjFunction> function) {
  // Allocate the upvalue array first so it doesn't cause the closure
  // to get collected.
  OID<OID<ObjUpvalue> > upvaluesOID = ALLOCATE(OID<ObjUpvalue>, function.lp()->upvalueCount);
  OID<ObjUpvalue>* upvalues = upvaluesOID.lp();
  for (int i = 0; i < function.lp()->upvalueCount; i++) {
    upvalues[i] = CB_NULL_OID;
  }

  OID<ObjClosure> closureOID= ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  ObjClosure* closure = closureOID.lp();
  closure->function = function;
  closure->upvalues = upvaluesOID;
  closure->upvalueCount = function.lp()->upvalueCount;
  return closureOID;
}
//< Closures not-yet
//> Calls and Functions not-yet

OID<ObjFunction> newFunction() {
  OID<ObjFunction> functionOID= ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  ObjFunction* function = functionOID.lp();

  function->arity = 0;
//> Closures not-yet
  function->upvalueCount = 0;
//< Closures not-yet
  function->name = CB_NULL_OID;
  initChunk(&function->chunk);
  return functionOID;
}
//< Calls and Functions not-yet
//> Classes and Instances not-yet

OID<ObjInstance> newInstance(OID<ObjClass> klass) {
  OID<ObjInstance> instanceOID = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
  ObjInstance* instance = instanceOID.lp();
  instance->klass = klass;
  initTable(&instance->fields, &clox_value_shallow_comparator, &clox_value_render);
  return instanceOID;
}
//< Classes and Instances not-yet
//> Calls and Functions not-yet

OID<ObjNative> newNative(NativeFn function) {
  OID<ObjNative> nativeOID = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  ObjNative* native = nativeOID.lp();
  native->function = function;
  return nativeOID;
}
//< Calls and Functions not-yet

/* Strings allocate-string < Hash Tables allocate-string
static ObjString* allocateXString(char* chars, int length) {
*/
//> allocate-string
//> Hash Tables allocate-string
static OID<ObjString> allocateString(OID<char> adoptedChars, int length,
                                     uint32_t hash) {
//< Hash Tables allocate-string
  OID<ObjString> stringOID = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  ObjString* string = stringOID.lp();
  string->length = length;
  string->chars = adoptedChars;
//> Hash Tables allocate-store-hash
  string->hash = hash;
//< Hash Tables allocate-store-hash

//> Garbage Collection not-yet
  Value stringValue = OBJ_VAL(stringOID.id());
  push(stringValue);
//< Garbage Collection not-yet
//> Hash Tables allocate-store-string
  printf("DANDEBUG interned string@%ju\"%.*s\"(%ju)\n",
         (uintmax_t)stringOID.id().id,
         length,
         adoptedChars.lp(),
         adoptedChars.id().id);
  tableSet(&vm.strings, stringValue, stringValue);
//> Garbage Collection not-yet
  pop();
//< Garbage Collection not-yet

//< Hash Tables allocate-store-string
  return stringOID;
}
//< allocate-string
//> Hash Tables hash-string
static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;

  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }

  return hash;
}
//< Hash Tables hash-string

//NOTE: 'length' does not include the null-terminator.
OID<ObjString> rawAllocateString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);

  OID<char> heapCharsOID = ALLOCATE(char, length + 1);
  char* heapChars = heapCharsOID.lp();
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  OID<ObjString> stringOID = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  ObjString* string = stringOID.lp();
  string->length = length;
  string->chars = heapCharsOID;
  string->hash = hash;

  printf("DANDEBUG rawAllocateString() created new string@%ju\"%s\"(%ju)\n",
         (uintmax_t)stringOID.id().id,
         heapChars,
         (uintmax_t)heapCharsOID.id().id);

  return stringOID;
}

//> take-string
OID<ObjString> takeString(OID<char> /*char[]*/ adoptedChars, int length) {
/* Strings take-string < Hash Tables take-string-hash
  return allocateXString(chars, length);
*/
//> Hash Tables take-string-hash
  uint32_t hash = hashString(adoptedChars.lp(), length);
//> take-string-intern
  OID<ObjString> internedOID = tableFindString(&vm.strings, adoptedChars.lp(), length,
                                                hash);
  if (!internedOID.is_nil()) {
    FREE_ARRAY(char, adoptedChars, length + 1);
    printf("DANDEBUG takeString() interned rawchars\"%.*s\"(%ju) to string@%ju\"%s\"(%ju)\n",
           length,
           adoptedChars.lp(),
           (uintmax_t)adoptedChars.id().id,
           (uintmax_t)internedOID.id().id,
           internedOID.lp()->chars.lp(),
           internedOID.lp()->chars.id().id);
    return internedOID;
  }

    printf("DANDEBUG takeString() could not find interned string for rawchars\"%.*s\"(%ju)\n",
           length,
           adoptedChars.lp(),
           (uintmax_t)adoptedChars.id().id);
//< take-string-intern
  return allocateString(adoptedChars, length, hash);
//< Hash Tables take-string-hash
}

//< take-string
OID<ObjString> copyString(const char* chars, int length) {
//> Hash Tables copy-string-hash
  uint32_t hash = hashString(chars, length);
//> copy-string-intern
  OID<ObjString> internedOID = tableFindString(&vm.strings, chars, length,
                                               hash);
  if (!internedOID.is_nil()) {
    printf("DANDEBUG copyString() interned C-string \"%.*s\" to string@%ju\"%s\"(%ju)\n",
           length,
           chars,
           (uintmax_t)internedOID.id().id,
           internedOID.lp()->chars.lp(),
           internedOID.lp()->chars.id().id);
    return internedOID;
  }
//< copy-string-intern

  printf("DANDEBUG copyString() could not find interned string for C-string \"%.*s\"\n",
         length, chars);

//< Hash Tables copy-string-hash
  OID<char> heapCharsOID = ALLOCATE(char, length + 1);
  char* heapChars = heapCharsOID.lp();
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

/* Strings object-c < Hash Tables copy-string-allocate
  return allocateXString(heapChars, length);
*/
//> Hash Tables copy-string-allocate
  return allocateString(heapCharsOID, length, hash);
//< Hash Tables copy-string-allocate
}
//> Closures not-yet

OID<ObjUpvalue> newUpvalue(unsigned int valueStackIndex) {
  OID<ObjUpvalue> upvalueOID = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  ObjUpvalue* upvalue = upvalueOID.lp();
  upvalue->closed = NIL_VAL;
  upvalue->valueStackIndex = valueStackIndex;
  upvalue->next = CB_NULL_OID;

  return upvalueOID;
}
//< Closures not-yet
//> print-object
void printObject(Value value) {
  if (IS_NIL(value)) {
    printf("nilval");
    return;
  }

  switch (OBJ_TYPE(value)) {
//> Classes and Instances not-yet
    case OBJ_CLASS:
      printf("%s", AS_CLASS_OID(value).lp()->name.lp()->chars.lp());
      break;
//< Classes and Instances not-yet
//> Methods and Initializers not-yet
    case OBJ_BOUND_METHOD:
      printf("<fn %s>",
             AS_BOUND_METHOD_OID(value).lp()->method.lp()->function.lp()->name.lp()->chars.lp());
      break;
//< Methods and Initializers not-yet
//> Closures not-yet
    case OBJ_CLOSURE:
      if (!AS_CLOSURE_OID(value).lp()->function.is_nil() &&
          !AS_CLOSURE_OID(value).lp()->function.lp()->name.is_nil()) {
        printf("<fn %s>", AS_CLOSURE_OID(value).lp()->function.lp()->name.lp()->chars.lp());
      } else {
        printf("<fn uninit%ju>", (uintmax_t)AS_CLOSURE_OID(value).lp()->function.id().id);
      }
      break;
//< Closures not-yet
//> Calls and Functions not-yet
    case OBJ_FUNCTION: {
      if (!AS_FUNCTION_OID(value).lp()->name.is_nil()) {
        printf("<fn %p>", AS_FUNCTION_OID(value).lp()->name.lp()->chars.lp());
      } else {
        printf("<fn anon%ju>", (uintmax_t)AS_FUNCTION_OID(value).id().id);
      }
      break;
    }
//< Calls and Functions not-yet
//> Classes and Instances not-yet
    case OBJ_INSTANCE:
      printf("%s instance", AS_INSTANCE_OID(value).lp()->klass.lp()->name.lp()->chars.lp());
      break;
//< Classes and Instances not-yet
//> Calls and Functions not-yet
    case OBJ_NATIVE:
      printf("<native fn>");
      break;
//< Calls and Functions not-yet
    case OBJ_STRING:
      printf("%s", AS_CSTRING(value));
      break;
//> Closures not-yet
    case OBJ_UPVALUE:
      printf("upvalue");
      break;
//< Closures not-yet
    default:
      printf("#?BADOBJ?#");
      break;
  }
}
//< print-object
