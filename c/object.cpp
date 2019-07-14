//> Strings object-c
#include <stdio.h>
#include <string.h>

#include "cb_bst.h"

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

static const char *
objTypeString(ObjType objType)
{
  switch (objType)
  {
    case OBJ_BOUND_METHOD: return "ObjBoundMethod";
    case OBJ_CLASS:        return "ObjClass";
    case OBJ_CLOSURE:      return "ObjClosure";
    case OBJ_FUNCTION:     return "ObjFunction";
    case OBJ_INSTANCE:     return "ObjInstance";
    case OBJ_NATIVE:       return "ObjNative";
    case OBJ_STRING:       return "ObjString";
    case OBJ_UPVALUE:      return "ObjUpvalue";
    default:               return "Obj???";
  }
}

static ObjID allocateObject(size_t size, size_t alignment, ObjType type) {
  CBO<Obj> objectCBO = reallocate(CB_NULL, 0, size, alignment, false);
  OID<Obj> objectOID = objtable_add(&thread_objtable, objectCBO.mo());

  Obj* object = objectCBO.mlp();
  object->type = type;
//> add-to-list
  object->next = vm.objects;
  vm.objects = objectOID;
//< add-to-list
//> Garbage Collection not-yet

#ifdef DEBUG_TRACE_GC
  //printf("%p allocate %ld for %d\n", object, size, type);
  printf("#%ju@%ju %s object allocated (%ld bytes)\n",
         (uintmax_t)objectOID.id().id,
         (uintmax_t)objectCBO.co(),
         objTypeString(type),
         size);
#endif

//< Garbage Collection not-yet
  return objectOID.id();
}
//< allocate-object
//> Methods and Initializers not-yet

OID<ObjBoundMethod> newBoundMethod(Value receiver, OID<ObjClosure> method) {
  OID<ObjBoundMethod> boundOID = ALLOCATE_OBJ(ObjBoundMethod,
                                       OBJ_BOUND_METHOD);
  ObjBoundMethod* bound = boundOID.mlip();

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
  int ret;

//< Superclasses not-yet
  OID<ObjClass> klassOID = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
  ObjClass* klass = klassOID.mlip();
  klass->name = name;
//> Superclasses not-yet
  klass->superclass = superclass;
//< Superclasses not-yet
//> Methods and Initializers not-yet
  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(klass->methods_bst),
                    &clox_value_shallow_comparator,
                    &clox_value_render,
                    &clox_value_external_size);
  assert(ret == 0);
  (void)ret;
//< Methods and Initializers not-yet
  return klassOID;
}
//< Classes and Instances not-yet
//> Closures not-yet

OID<ObjClosure> newClosure(OID<ObjFunction> function) {
  // Allocate the upvalue array first so it doesn't cause the closure
  // to get collected.
  CBO<OID<ObjUpvalue> > upvaluesCBO = ALLOCATE(OID<ObjUpvalue>, function.clip()->upvalueCount);
  OID<ObjUpvalue>* upvalues = upvaluesCBO.mlp();
  for (int i = 0; i < function.clip()->upvalueCount; i++) {
    upvalues[i] = CB_NULL_OID;
  }

  OID<ObjClosure> closureOID = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
  ObjClosure* closure = closureOID.mlip();
  closure->function = function;
  closure->upvalues = upvaluesCBO;
  closure->upvalueCount = function.clip()->upvalueCount;
  return closureOID;
}
//< Closures not-yet
//> Calls and Functions not-yet

OID<ObjFunction> newFunction() {
  OID<ObjFunction> functionOID = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
  ObjFunction* function = functionOID.mlip();

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
  int ret;
  OID<ObjInstance> instanceOID = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
  ObjInstance* instance = instanceOID.mlip();
  instance->klass = klass;
  ret = cb_bst_init(&thread_cb,
                    &thread_region,
                    &(instance->fields_bst),
                    &clox_value_shallow_comparator,
                    &clox_value_render,
                    &clox_value_external_size);
  assert(ret == 0);
  (void)ret;

  return instanceOID;
}
//< Classes and Instances not-yet
//> Calls and Functions not-yet

OID<ObjNative> newNative(NativeFn function) {
  OID<ObjNative> nativeOID = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
  ObjNative* native = nativeOID.mlip();
  native->function = function;
  return nativeOID;
}
//< Calls and Functions not-yet

/* Strings allocate-string < Hash Tables allocate-string
static ObjString* allocateXString(char* chars, int length) {
*/
//> allocate-string
//> Hash Tables allocate-string
static OID<ObjString> allocateString(CBO<char> adoptedChars, int length,
                                     uint32_t hash) {
//< Hash Tables allocate-string
  OID<ObjString> stringOID = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  ObjString* string = stringOID.mlip();
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
  printf("DANDEBUG interned string#%ju@%ju\"%.*s\"\n",
         (uintmax_t)stringOID.id().id,
         (uintmax_t)adoptedChars.co(),
         length,
         adoptedChars.clp());
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

  CBO<char> heapCharsCBO = ALLOCATE(char, length + 1);
  char* heapChars = heapCharsCBO.mlp();
  memcpy(heapChars, chars, length);
  heapChars[length] = '\0';

  OID<ObjString> stringOID = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  ObjString* string = stringOID.mlip();
  string->length = length;
  string->chars = heapCharsCBO;
  string->hash = hash;

  printf("DANDEBUG rawAllocateString() created new string#%ju@%ju\"%s\"\n",
         (uintmax_t)stringOID.id().id,
         (uintmax_t)stringOID.co(),
         heapChars);

  return stringOID;
}

//> take-string
OID<ObjString> takeString(CBO<char> /*char[]*/ adoptedChars, int length) {
/* Strings take-string < Hash Tables take-string-hash
  return allocateXString(chars, length);
*/
//> Hash Tables take-string-hash
  uint32_t hash = hashString(adoptedChars.clp(), length);
//> take-string-intern
  OID<ObjString> internedOID = tableFindString(&vm.strings, adoptedChars.co(), adoptedChars.clp(), length,
                                                hash);
  if (!internedOID.is_nil()) {
    FREE_ARRAY(char, adoptedChars.co(), length + 1);
    printf("DANDEBUG takeString() interned rawchars@%ju\"%.*s\" to string#%ju@%ju\"%s\"\n",
           (uintmax_t)adoptedChars.co(),
           length,
           adoptedChars.clp(),
           (uintmax_t)internedOID.id().id,
           (uintmax_t)internedOID.co(),
           internedOID.clip()->chars.clp());
    return internedOID;
  }

    printf("DANDEBUG takeString() could not find interned string for rawchars@%ju\"%.*s\"\n",
           (uintmax_t)adoptedChars.co(),
           length,
           adoptedChars.clp());
//< take-string-intern
  return allocateString(adoptedChars, length, hash);
//< Hash Tables take-string-hash
}

//< take-string
OID<ObjString> copyString(const char* chars, int length) {
//> Hash Tables copy-string-hash
  uint32_t hash = hashString(chars, length);
//> copy-string-intern
  OID<ObjString> internedOID = tableFindString(&vm.strings, CB_NULL, chars, length,
                                               hash);
  if (!internedOID.is_nil()) {
    printf("DANDEBUG copyString() interned C-string \"%.*s\" to string#%ju@%ju\"%s\"\n",
           length,
           chars,
           (uintmax_t)internedOID.id().id,
           (uintmax_t)internedOID.clip()->chars.co(),
           internedOID.clip()->chars.clp());
    return internedOID;
  }
//< copy-string-intern

  printf("DANDEBUG copyString() could not find interned string for C-string \"%.*s\"\n",
         length, chars);

//< Hash Tables copy-string-hash
  CBO<char> heapCharsCBO = ALLOCATE(char, length + 1);
  char* heapChars = heapCharsCBO.mlp();
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

OID<ObjUpvalue> newUpvalue(unsigned int valueStackIndex) {
  OID<ObjUpvalue> upvalueOID = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
  ObjUpvalue* upvalue = upvalueOID.mlip();
  upvalue->closed = NIL_VAL;
  upvalue->valueStackIndex = valueStackIndex;
  upvalue->next = CB_NULL_OID;

  return upvalueOID;
}
//< Closures not-yet
//> print-object
void printObject(ObjID id, cb_offset_t offset, const Obj *obj) {
  if (!obj) {
    printf("nilobj");
    return;
  }

  switch (obj->type) {
    case OBJ_CLASS:
      printf("class#%ju@%ju\"%s\"",
             (uintmax_t)id.id,
             (uintmax_t)offset,
             ((const ObjClass *)obj)->name.clip()->chars.clp());
      break;

    case OBJ_BOUND_METHOD:
      printf("boundmethod#%ju@%ju\"%s\"",
             (uintmax_t)id.id,
             (uintmax_t)offset,
             ((const ObjBoundMethod *)obj)->method.clip()->function.clip()->name.clip()->chars.clp());
      break;

    case OBJ_CLOSURE: {
      const ObjClosure *clo = (const ObjClosure *)obj;
      if (!clo->function.is_nil() &&
          !clo->function.clip()->name.is_nil()) {
        printf("closure#%ju@%ju(fun#%ju@%ju\"%s\")",
               (uintmax_t)id.id,
               (uintmax_t)offset,
               (uintmax_t)clo->function.id().id,
               (uintmax_t)clo->function.co(),
               clo->function.clip()->name.clip()->chars.clp());
      } else {
        printf("closure#%ju@%ju(fun#%ju@%ju<anon>)",
               (uintmax_t)id.id,
               (uintmax_t)offset,
               (uintmax_t)clo->function.id().id,
               (uintmax_t)clo->function.co());
      }
      break;
    }

    case OBJ_FUNCTION: {
      const ObjFunction *fun = (const ObjFunction *)obj;
      if (!fun->name.is_nil()) {
        printf("fun#%ju@%ju\"%s\"",
               (uintmax_t)id.id,
               (uintmax_t)offset,
               fun->name.clip()->chars.clp());
      } else {
        printf("fun#%ju@%ju<anon>",
               (uintmax_t)id.id,
               (uintmax_t)offset);
      }
      break;
    }

    case OBJ_INSTANCE:
      printf("instance#%ju@%ju\"%s\"",
             (uintmax_t)id.id,
             (uintmax_t)offset,
             ((const ObjInstance *)obj)->klass.clip()->name.clip()->chars.clp());
      break;

    case OBJ_NATIVE:
      printf("native#%ju@%ju:%p",
             (uintmax_t)id.id,
             (uintmax_t)offset,
             (void*)(uintptr_t)((const ObjNative*)obj)->function);
      break;

    case OBJ_STRING:
      printf("string#%ju@%ju\"%s\"",
             (uintmax_t)id.id,
             (uintmax_t)offset,
             ((const ObjString *)obj)->chars.clp());
      break;

    case OBJ_UPVALUE:
      printf("upvalue#%ju@%ju",
             (uintmax_t)id.id,
             (uintmax_t)offset);
      break;

    default:
      printf("#?BADOBJ?#");
      break;
  }
}

void printObjectValue(Value value) {
  OID<Obj> tmp = AS_OBJ_ID(value);
  printObject(tmp.id(), tmp.co(), tmp.clip());
}
//< print-object
