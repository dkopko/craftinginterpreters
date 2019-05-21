//> Chunks of Bytecode value-h
#ifndef clox_value_h
#define clox_value_h

#include "cb_integration.h"
#include "common.h"
#include <assert.h>

//> Strings forward-declare-obj
typedef struct sObj Obj;
//> forward-declare-obj-string
typedef struct sObjString ObjString;
//< forward-declare-obj-string

//< Optimization not-yet
//> Types of Values value-type
typedef enum {
  VAL_BOOL,
  VAL_NIL, // [user-types]
  VAL_NUMBER,
//> Strings val-obj
  VAL_OBJ
//< Strings val-obj
} ValueType;

//< Strings forward-declare-obj
//> Optimization not-yet
#ifdef NAN_TAGGING

// A mask that selects the sign bit.
#define SIGN_BIT ((uint64_t)1 << 63)

// The bits that must be set to indicate a quiet NaN.
#define QNAN ((uint64_t)0x7ffc000000000000)

// Tag values for the different singleton values.
#define TAG_NIL       1 // 001
#define TAG_FALSE     2 // 010
#define TAG_TRUE      3 // 011
#define TAG_TOMBSTONE 4 // 100

typedef struct { uint64_t val; } Value;

#define IS_BOOL(v)    (((v.val) & (SIGN_BIT | QNAN | TAG_FALSE)) == (~SIGN_BIT | QNAN | TAG_FALSE))
#define IS_NIL(v)     ((v.val) == NIL_VAL.val)
// If the NaN bits are set, it's not a number.
#define IS_NUMBER(v)  (((v.val) & QNAN) != QNAN)
#define IS_OBJ(v)     (((v.val) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(v)    ((v.val) == TRUE_VAL.val)
#define AS_NUMBER(v)  valueToNum(v)
#define AS_OBJ_ID(v)     ((ObjID) { (v.val) & ~(SIGN_BIT | QNAN) })
#define AS_OBJ(v)     ((Obj*)cb_at(thread_cb, objtable_lookup(&thread_objtable, AS_OBJ_ID(v))))

#define BOOL_VAL(boolean) ((Value) { ((boolean) ? TRUE_VAL : FALSE_VAL) })
#define FALSE_VAL         ((Value) { (uint64_t)(QNAN | TAG_FALSE) })
#define TRUE_VAL          ((Value) { (uint64_t)(QNAN | TAG_TRUE) })
#define NIL_VAL           ((Value) { (uint64_t)(QNAN | TAG_NIL) })
#define TOMBSTONE_VAL     ((Value) { (uint64_t)(QNAN | TAG_TOMBSTONE) })
#define NUMBER_VAL(num)   numToValue(num)
// The triple casting is necessary here to satisfy some compilers:
// 1. CBINT REMOVED (uintptr_t) Convert the pointer to a number of the right size.
// 2. (uint64_t)  Pad it up to 64 bits in 32-bit builds.
// 3. Or in the bits to make a tagged Nan.
// 4. Cast to a typedef'd value.
#define OBJ_VAL(objid) \
    ((Value) { (SIGN_BIT | QNAN | (uint64_t)(objid.id)) })

// A union to let us reinterpret a double as raw bits and back.
typedef union {
  uint64_t bits64;
  uint32_t bits32[2];
  double num;
} DoubleUnion;

static inline double valueToNum(Value value) {
  DoubleUnion data;
  data.bits64 = value.val;
  return data.num;
}

static inline Value numToValue(double num) {
  DoubleUnion data;
  data.num = num;
  return (Value) { data.bits64 };
}

static inline ValueType getValueType(Value v) {
  if (IS_BOOL(v)) return VAL_BOOL;
  if (IS_NIL(v)) return VAL_NIL;
  if (IS_NUMBER(v)) return VAL_NUMBER;
  assert(IS_OBJ(v));
  return VAL_OBJ;
}
#else

//< Types of Values value-type
/* Chunks of Bytecode value-h < Types of Values value
typedef double Value;
*/
//> Types of Values value
typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
//> Strings union-object
    cb_offset_t /* Obj* */ obj;
//< Strings union-object
  } as; // [as]
} Value;
//< Types of Values value
//> Types of Values is-macros

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
//> Strings is-obj
#define IS_OBJ(value)     ((value).type == VAL_OBJ)
//< Strings is-obj
//< Types of Values is-macros
//> Types of Values as-macros

//> Strings as-obj
#define AS_OBJ(value)     ((Obj*)cb_at(thread_cb, (value).as.obj))
//< Strings as-obj
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
//< Types of Values as-macros
//> Types of Values value-macros

#define BOOL_VAL(value)   ((Value){ VAL_BOOL, { .boolean = value } })
#define NIL_VAL           ((Value){ VAL_NIL, { .number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })
//> Strings obj-val
#define OBJ_VAL(object_offset)   ((Value){ VAL_OBJ, { .obj = object_offset } })
//< Strings obj-val
//< Types of Values value-macros
//> Optimization not-yet

#endif
//< Optimization not-yet
//> value-array

typedef struct {
  int capacity;
  int count;
  OID<Value> values;
} ValueArray;
//< value-array
//> array-fns-h

//> Types of Values values-equal-h
bool valuesEqual(Value a, Value b);
//< Types of Values values-equal-h
void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
//< array-fns-h
//> print-value-h
void printValue(Value value);
//< print-value-h

#endif
