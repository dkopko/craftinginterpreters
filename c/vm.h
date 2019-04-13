//> A Virtual Machine vm-h
#ifndef clox_vm_h
#define clox_vm_h

/* A Virtual Machine vm-h < Calls and Functions not-yet
#include "chunk.h"
*/
//> Calls and Functions not-yet
#include "object.h"
//< Calls and Functions not-yet
//> Hash Tables vm-include-table
#include "table.h"
//< Hash Tables vm-include-table
//> vm-include-value
#include "value.h"
//< vm-include-value
/* A Virtual Machine stack-max < Calls and Functions not-yet

#define STACK_MAX 256
*/
//> Calls and Functions not-yet
// TODO: Don't depend on frame count for stack count since we have
// stack before frames?
#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef struct {
/* Calls and Functions not-yet < Closures not-yet
  ObjFunction* function;
*/
//> Closures not-yet
  CBO<ObjClosure> closure;
//< Closures not-yet
  uint8_t* ip;
  Value* slots;
  unsigned int slotsIndex;  // the index within the stack where slots field points.
  unsigned int slotsCount;
} CallFrame;
//< Calls and Functions not-yet

typedef struct {
  //CBINT FIXME cache Value *astack, *bstack, *cstack in self-correcting pointer
  // objects to speed access but allow for CB resizes?

  cb_offset_t  abo; // A base offset (mutable region)
  unsigned int abi; // A base index  (mutable region)
  cb_offset_t  bbo; // B base offset
  unsigned int bbi; // B base index
  cb_offset_t  cbo; // C base offset
  unsigned int cbi; // C base index (always 0, really)
  unsigned int stackDepth;  // [0, stack_depth-1] are valid entries.
} TriStack;

typedef struct {
/* A Virtual Machine vm-h < Calls and Functions not-yet
  Chunk* chunk;
*/
/* A Virtual Machine ip < Calls and Functions not-yet
  uint8_t* ip;
*/
//> vm-stack
  TriStack tristack;
//< vm-stack
//> Calls and Functions not-yet

  CallFrame frames[FRAMES_MAX];
  int frameCount;

//< Calls and Functions not-yet
//> Global Variables vm-globals
  Table globals;
//< Global Variables vm-globals
//> Hash Tables vm-strings
  Table strings;
//< Hash Tables vm-strings
//> Methods and Initializers not-yet
  CBO<ObjString> initString;
//< Methods and Initializers not-yet
//> Closures not-yet
  CBO<ObjUpvalue> openUpvalues;  //Head of singly-linked openUpvalue list, not an array.
//< Closures not-yet
//> Garbage Collection not-yet

  size_t bytesAllocated;
  size_t nextGC;
//< Garbage Collection not-yet
//> Strings objects-root

  Obj* objects;
//< Strings objects-root
//> Garbage Collection not-yet
  int grayCount;
  int grayCapacity;
  Obj** grayStack;
//< Garbage Collection not-yet
} VM;

//> interpret-result
typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

//< interpret-result
//> Strings extern-vm
extern VM vm;

//< Strings extern-vm
void initVM();
void freeVM();
/* A Virtual Machine interpret-h < Scanning on Demand vm-interpret-h
InterpretResult interpret(Chunk* chunk);
*/
//> Scanning on Demand vm-interpret-h
InterpretResult interpret(const char* source);
//< Scanning on Demand vm-interpret-h
//> push-pop
void push(Value value);
Value pop();
//< push-pop

#endif
