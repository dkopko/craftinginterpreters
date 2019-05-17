//> Strings object-h
#ifndef clox_object_h
#define clox_object_h

#include "cb_integration.h"
#include "common.h"
//> Calls and Functions not-yet
#include "chunk.h"
//< Calls and Functions not-yet
//> Classes and Instances not-yet
#include "table.h"
//< Classes and Instances not-yet
#include "value.h"
//> obj-type-macro

#define OBJ_TYPE(value)         (AS_OBJ(value)->type)
//< obj-type-macro
//> is-string

//> Methods and Initializers not-yet
#define IS_BOUND_METHOD(value)  isObjType(value, OBJ_BOUND_METHOD)
//< Methods and Initializers not-yet
//> Classes and Instances not-yet
#define IS_CLASS(value)         isObjType(value, OBJ_CLASS)
//< Classes and Instances not-yet
//> Closures not-yet
#define IS_CLOSURE(value)       isObjType(value, OBJ_CLOSURE)
//< Closures not-yet
//> Calls and Functions not-yet
#define IS_FUNCTION(value)      isObjType(value, OBJ_FUNCTION)
//< Calls and Functions not-yet
//> Classes and Instances not-yet
#define IS_INSTANCE(value)      isObjType(value, OBJ_INSTANCE)
//< Classes and Instances not-yet
//> Calls and Functions not-yet
#define IS_NATIVE(value)        isObjType(value, OBJ_NATIVE)
//< Calls and Functions not-yet
#define IS_STRING(value)        isObjType(value, OBJ_STRING)
//< is-string
//> as-string

//> Methods and Initializers not-yet
#define AS_BOUND_METHOD_OID(value)  (OID<ObjBoundMethod>(AS_OBJ_ID(value)))
//< Methods and Initializers not-yet
//> Classes and Instances not-yet
#define AS_CLASS_OID(value)         (OID<ObjClass>(AS_OBJ_ID(value)))
//< Classes and Instances not-yet
//> Closures not-yet
#define AS_CLOSURE_OID(value)       (OID<ObjClosure>(AS_OBJ_ID(value)))
//< Closures not-yet
//> Calls and Functions not-yet
#define AS_FUNCTION_OID(value)      (OID<ObjFunction>(AS_OBJ_ID(value)))
//< Calls and Functions not-yet
//> Classes and Instances not-yet
#define AS_INSTANCE_OID(value)      (OID<ObjInstance>(AS_OBJ_ID(value)))
//< Classes and Instances not-yet
#define AS_UPVALUE_OID(value)      (OID<ObjUpvalue>(AS_OBJ_ID(value)))
//> Calls and Functions not-yet
#define AS_NATIVE(value)        ((OID<ObjNative>(AS_OBJ_ID(value)).lp())->function)
//< Calls and Functions not-yet
#define AS_STRING_OID(value)        (OID<ObjString>(AS_OBJ_ID(value)))
#define AS_CSTRING(value)       ((OID<ObjString>(AS_OBJ_ID(value)).lp())->chars.lp())
//< as-string
//> obj-type

typedef enum {
//> Methods and Initializers not-yet
  OBJ_BOUND_METHOD,
//< Methods and Initializers not-yet
//> Classes and Instances not-yet
  OBJ_CLASS,
//< Classes and Instances not-yet
//> Closures not-yet
  OBJ_CLOSURE,
//< Closures not-yet
//> Calls and Functions not-yet
  OBJ_FUNCTION,
//< Calls and Functions not-yet
//> Classes and Instances not-yet
  OBJ_INSTANCE,
//< Classes and Instances not-yet
//> Calls and Functions not-yet
  OBJ_NATIVE,
//< Calls and Functions not-yet
  OBJ_STRING,
//> Closures not-yet
  OBJ_UPVALUE
//< Closures not-yet
} ObjType;
//< obj-type

struct sObj {
  ObjType type;
//> Garbage Collection not-yet
  bool isDark;
//< Garbage Collection not-yet
//> next-field
  OID<struct sObj> next;
//< next-field
};
//> Calls and Functions not-yet

typedef struct {
  Obj obj;
  int arity;
//> Closures not-yet
  int upvalueCount;
//< Closures not-yet
  Chunk chunk;
  OID<ObjString> name;
} ObjFunction;

typedef Value (*NativeFn)(int argCount, Value* args);

typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;
//< Calls and Functions not-yet
//> obj-string

struct sObjString {
  Obj obj;
  int length;
  OID<char> chars; // char[]
//> Hash Tables obj-string-hash
  uint32_t hash;
//< Hash Tables obj-string-hash
};
//< obj-string
//> Closures not-yet

typedef struct sUpvalue {
  Obj obj;

  // Pointer to the variable this upvalue is referencing.
  int valueStackIndex;

  // If the upvalue is closed (i.e. the local variable it was pointing
  // to has been popped off the stack) then the closed-over value is
  // hoisted out of the stack into here. [value] is then be changed to
  // point to this.
  Value closed;

  // Open upvalues are stored in a linked list. This points to the next
  // one in that list.
  OID<struct sUpvalue> next;  //CBINT FIXME How will this linked list work under collection?
} ObjUpvalue;

typedef struct {
  Obj obj;
  OID<ObjFunction> function;
  OID<OID<ObjUpvalue> > upvalues;  //pointer to ObjUpvalue[] (used to be type ObjUpvalue**).
  int upvalueCount;
} ObjClosure;
//< Closures not-yet
//> Classes and Instances not-yet

typedef struct sObjClass {
  Obj obj;
  OID<ObjString> name;
//> Superclasses not-yet
  OID<struct sObjClass> superclass;  //struct sObjClass* (only pointer, not array).
//< Superclasses not-yet
//> Methods and Initializers not-yet
  Table methods;
//< Methods and Initializers not-yet
} ObjClass;

typedef struct {
  Obj obj;
  OID<ObjClass> klass;
  Table fields;
} ObjInstance;
//< Classes and Instances not-yet

//> Methods and Initializers not-yet
typedef struct {
  Obj obj;
  Value receiver;
  OID<ObjClosure> method;
} ObjBoundMethod;

OID<ObjBoundMethod> newBoundMethod(Value receiver, OID<ObjClosure> method);
//< Methods and Initializers not-yet
/* Classes and Instances not-yet < Superclasses not-yet
ObjClass* newClass(ObjString* name);
*/
//> Superclasses not-yet
OID<ObjClass> newClass(OID<ObjString> name, OID<ObjClass> superclass);
//< Superclasses not-yet
//> Closures not-yet
OID<ObjClosure> newClosure(OID<ObjFunction> function);
//< Closures not-yet
//> Calls and Functions not-yet
OID<ObjFunction> newFunction();
//< Calls and Functions not-yet
//> Classes and Instances not-yet
OID<ObjInstance> newInstance(OID<ObjClass> klass);
//< Classes and Instances not-yet
//> Calls and Functions not-yet
OID<ObjNative> newNative(NativeFn function);
//< Calls and Functions not-yet
OID<ObjString> rawAllocateString(const char* chars, int length);
//> take-string-h
OID<ObjString> takeString(OID<char> /*char[]*/ chars, int length);
//< take-string-h
//> copy-string-h
OID<ObjString> copyString(const char* chars, int length);

//< copy-string-h
//> Closures not-yet
OID<ObjUpvalue> newUpvalue(unsigned int valueStackIndex);
//< Closures not-yet
//> print-object-h
void printObject(Value value);
//< print-object-h
//> is-obj-type
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

//< is-obj-type
#endif
