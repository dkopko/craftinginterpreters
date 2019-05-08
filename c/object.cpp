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

static cb_offset_t allocateObject(size_t size, size_t alignment, ObjType type) {
  CBO<Obj> objectCBO = reallocate(CB_NULL, 0, size, alignment);
  Obj* object = objectCBO.lp();
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
  printf("@%ju object was allocated (%ld bytes for type %d)\n", objectCBO.o(), size, type);
#endif

//< Garbage Collection not-yet
  return objectCBO.o();
}
//< allocate-object
//> Methods and Initializers not-yet

CBO<ObjBoundMethod> newBoundMethod(Value receiver, CBO<ObjClosure> method) {
  CBO<ObjBoundMethod> boundCBO = ALLOCATE_OBJ(ObjBoundMethod,
                                       OBJ_BOUND_METHOD);
  ObjBoundMethod* bound = boundCBO.lp();

  bound->receiver = receiver;
  bound->method = method;
  return boundCBO.o();
}
//< Methods and Initializers not-yet
//> Classes and Instances not-yet

/* Classes and Instances not-yet < Superclasses not-yet
ObjClass* newClass(ObjString* name) {
*/
//> Superclasses not-yet
CBO<ObjClass> newClass(CBO<ObjString> name, CBO<ObjClass> superclass) {
//< Superclasses not-yet
  CBO<ObjClass> klassCBO = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
  ObjClass* klass = klassCBO.lp();
  klass->name = name;
//> Superclasses not-yet
  klass->superclass = superclass;
//< Superclasses not-yet
//> Methods and Initializers not-yet
  initTable(&klass->methods, &clox_value_shallow_comparator, &clox_value_render);
//< Methods and Initializers not-yet
  return klassCBO;
}
//< Classes and Instances not-yet
//> Closures not-yet

CBO<ObjClosure> newClosure(CBO<ObjFunction> function) {
  // Allocate the upvalue array first so it doesn't cause the closure
  // to get collected.
  CBO<CBO<ObjUpvalue> > upvaluesCBO = ALLOCATE(CBO<ObjUpvalue>, function.lp()->upvalueCount);
  CBO<ObjUpvalue>* upvalues = upvaluesCBO.lp();
  for (int i = 0; i < function.lp()->upvalueCount; i++) {
    upvalues[i] = CB_NULL;
  }

  CBO<ObjClosure> closureCBO = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  ObjClosure* closure = closureCBO.lp();
  closure->function = function;
  closure->upvalues = upvaluesCBO;
  closure->upvalueCount = function.lp()->upvalueCount;
  return closureCBO;
}
//< Closures not-yet
//> Calls and Functions not-yet

CBO<ObjFunction> newFunction() {
  CBO<ObjFunction> functionCBO = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  ObjFunction* function = functionCBO.lp();

  function->arity = 0;
//> Closures not-yet
  function->upvalueCount = 0;
//< Closures not-yet
  function->name = CB_NULL;
  initChunk(&function->chunk);
  return functionCBO;
}
//< Calls and Functions not-yet
//> Classes and Instances not-yet

CBO<ObjInstance> newInstance(CBO<ObjClass> klass) {
  CBO<ObjInstance> instanceCBO = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
  ObjInstance* instance = instanceCBO.lp();
  instance->klass = klass;
  initTable(&instance->fields, &clox_value_shallow_comparator, &clox_value_render);
  return instanceCBO;
}
//< Classes and Instances not-yet
//> Calls and Functions not-yet

CBO<ObjNative> newNative(NativeFn function) {
  CBO<ObjNative> nativeCBO = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  ObjNative* native = nativeCBO.lp();
  native->function = function;
  return nativeCBO;
}
//< Calls and Functions not-yet

/* Strings allocate-string < Hash Tables allocate-string
static ObjString* allocateXString(char* chars, int length) {
*/
//> allocate-string
//> Hash Tables allocate-string
static CBO<ObjString> allocateString(CBO<char> adoptedChars, int length,
                                      uint32_t hash) {
//< Hash Tables allocate-string
  CBO<ObjString> stringCBO = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  ObjString* string = stringCBO.lp();
  string->length = length;
  string->chars = adoptedChars;
//> Hash Tables allocate-store-hash
  string->hash = hash;
//< Hash Tables allocate-store-hash

//> Garbage Collection not-yet
  Value stringValue = OBJ_VAL(stringCBO.o());
  push(stringValue);
//< Garbage Collection not-yet
//> Hash Tables allocate-store-string
  printf("DANDEBUG interned string@%ju\"%.*s\"(%ju)\n",
         (uintmax_t)stringCBO.o(),
         length,
         adoptedChars.lp(),
         adoptedChars.o());
  tableSet(&vm.strings, stringValue, stringValue);
//> Garbage Collection not-yet
  pop();
//< Garbage Collection not-yet

//< Hash Tables allocate-store-string
  return stringCBO;
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
CBO<ObjString> rawAllocateString(const char* chars, int length) {
  uint32_t hash = hashString(chars, length);

  CBO<char> heapCharsCBO = ALLOCATE(char, length + 1);
  char* heapChars = heapCharsCBO.lp();
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  CBO<ObjString> stringCBO = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  ObjString* string = stringCBO.lp();
  string->length = length;
  string->chars = heapCharsCBO;
  string->hash = hash;

  printf("DANDEBUG rawAllocateString() created new string@%ju\"%s\"(%ju)\n",
         (uintmax_t)stringCBO.o(),
         heapChars,
         (uintmax_t)heapCharsCBO.o());

  return stringCBO;
}

//> take-string
CBO<ObjString> takeString(CBO<char> /*char[]*/ adoptedChars, int length) {
/* Strings take-string < Hash Tables take-string-hash
  return allocateXString(chars, length);
*/
//> Hash Tables take-string-hash
  uint32_t hash = hashString(adoptedChars.lp(), length);
//> take-string-intern
  CBO<ObjString> internedCBO = tableFindString(&vm.strings, adoptedChars.lp(), length,
                                                hash);
  if (!internedCBO.is_nil()) {
    FREE_ARRAY(char, adoptedChars, length + 1);
    printf("DANDEBUG takeString() interned rawchars\"%.*s\"(%ju) to string@%ju\"%s\"(%ju)\n",
           length,
           adoptedChars.lp(),
           (uintmax_t)adoptedChars.o(),
           (uintmax_t)internedCBO.o(),
           internedCBO.lp()->chars.lp(),
           internedCBO.lp()->chars.o());
    return internedCBO;
  }

    printf("DANDEBUG takeString() could not find interned string for rawchars\"%.*s\"(%ju)\n",
           length,
           adoptedChars.lp(),
           (uintmax_t)adoptedChars.o());
//< take-string-intern
  return allocateString(adoptedChars, length, hash);
//< Hash Tables take-string-hash
}

//< take-string
CBO<ObjString> copyString(const char* chars, int length) {
//> Hash Tables copy-string-hash
  uint32_t hash = hashString(chars, length);
//> copy-string-intern
  CBO<ObjString> internedCBO = tableFindString(&vm.strings, chars, length,
                                                hash);
  if (!internedCBO.is_nil()) {
    printf("DANDEBUG copyString() interned C-string \"%.*s\" to string@%ju\"%s\"(%ju)\n",
           length,
           chars,
           (uintmax_t)internedCBO.o(),
           internedCBO.lp()->chars.lp(),
           internedCBO.lp()->chars.o());
    return internedCBO;
  }
//< copy-string-intern

  printf("DANDEBUG copyString() could not find interned string for C-string \"%.*s\"\n",
         length, chars);

//< Hash Tables copy-string-hash
  CBO<char> heapCharsCBO = ALLOCATE(char, length + 1);
  char* heapChars = heapCharsCBO.lp();
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

/* Strings object-c < Hash Tables copy-string-allocate
  return allocateXString(heapChars, length);
*/
//> Hash Tables copy-string-allocate
  return allocateString(heapCharsCBO, length, hash);
//< Hash Tables copy-string-allocate
}
//> Closures not-yet

CBO<ObjUpvalue> newUpvalue(unsigned int valueStackIndex) {
  CBO<ObjUpvalue> upvalueCBO = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  ObjUpvalue* upvalue = upvalueCBO.lp();
  upvalue->closed = NIL_VAL;
  upvalue->valueStackIndex = valueStackIndex;
  upvalue->next = CB_NULL;

  return upvalueCBO;
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
      printf("%s", AS_CLASS_OFFSET(value).lp()->name.lp()->chars.lp());
      break;
//< Classes and Instances not-yet
//> Methods and Initializers not-yet
    case OBJ_BOUND_METHOD:
      printf("<fn %s>",
             AS_BOUND_METHOD_OFFSET(value).lp()->method.lp()->function.lp()->name.lp()->chars.lp());
      break;
//< Methods and Initializers not-yet
//> Closures not-yet
    case OBJ_CLOSURE:
      if (!AS_CLOSURE_OFFSET(value).lp()->function.is_nil() &&
          !AS_CLOSURE_OFFSET(value).lp()->function.lp()->name.is_nil()) {
        printf("<fn %s>", AS_CLOSURE_OFFSET(value).lp()->function.lp()->name.lp()->chars.lp());
      } else {
        printf("<fn uninit%ju>", (uintmax_t)AS_CLOSURE_OFFSET(value).lp()->function.o());
      }
      break;
//< Closures not-yet
//> Calls and Functions not-yet
    case OBJ_FUNCTION: {
      if (!AS_FUNCTION_OFFSET(value).lp()->name.is_nil()) {
        printf("<fn %p>", AS_FUNCTION_OFFSET(value).lp()->name.lp()->chars.lp());
      } else {
        printf("<fn anon%ju>", (uintmax_t)AS_FUNCTION_OFFSET(value).o());
      }
      break;
    }
//< Calls and Functions not-yet
//> Classes and Instances not-yet
    case OBJ_INSTANCE:
      printf("%s instance", AS_INSTANCE_OFFSET(value).lp()->klass.lp()->name.lp()->chars.lp());
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
