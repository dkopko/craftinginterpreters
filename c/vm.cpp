#include <assert.h>
//> A Virtual Machine vm-c
//> Types of Values include-stdarg
#include <stdarg.h>
//< Types of Values include-stdarg
//> vm-include-stdio
#include <stdio.h>
//> Strings vm-include-string
#include <string.h>
//< Strings vm-include-string
//> Calls and Functions not-yet
#include <time.h>
//< Calls and Functions not-yet

#include "cb_bst.h"

#include "cb_integration.h"
//< vm-include-stdio
#include "common.h"
//> Scanning on Demand vm-include-compiler
#include "compiler.h"
//< Scanning on Demand vm-include-compiler
//> vm-include-debug
#include "debug.h"
//< vm-include-debug
//> Strings vm-include-object-memory
#include "object.h"
#include "memory.h"
//< Strings vm-include-object-memory
#include "vm.h"

static void
tristack_reset(TriStack *ts) {
  cb_offset_t new_offset;
  int ret;

  printf("DANDEBUG before tristack reset\n");
  ret = cb_region_memalign(&thread_cb,
                           &thread_region,
                           &new_offset,
                           cb_alignof(Value),
                           sizeof(Value) * STACK_MAX);
  printf("DANDEBUG after tristack reset\n");
  (void)ret;

  ts->abo = new_offset;
  ts->abi = 0;
  ts->bbo = CB_NULL;
  ts->bbi = 0;
  ts->cbo = CB_NULL;
  ts->cbi = 0;
  ts->stackDepth = 0;
}

static Value*
tristack_at_bc(TriStack *ts, unsigned int index) {
  assert(index < ts->stackDepth);
  //CBINT FIXME - add a way to check maximum index of B subsection?

  cb_offset_t offset;

  //Use appropriate location amongst only B and C subsections.
  if (index < ts->bbi) {
    offset = ts->cbo + (index - ts->cbi) * sizeof(Value);
  } else {
    offset = ts->bbo + (index - ts->bbi) * sizeof(Value);
  }

  return static_cast<Value*>(cb_at(thread_cb, offset));
}

Value*
tristack_at(TriStack *ts, unsigned int index) {
  assert(index < ts->stackDepth);

  cb_offset_t offset;

  // Handle A section indices...
  if (index >= ts->abi) {
    offset = ts->abo + (index - ts->abi) * sizeof(Value);
    return static_cast<Value*>(cb_at(thread_cb, offset));
  }

  // Otherwise, fall back to B and C sections.
  return tristack_at_bc(ts, index);
}

const char *
tristack_regionname_at(TriStack *ts, unsigned int index) {
  if (index >= ts->abi) return "A";
  if (index >= ts->bbi) return "B";
  return "C";
}

static Value
tristack_peek(TriStack *ts, unsigned int down) {
  assert(down < ts->stackDepth);

  unsigned int ei = (ts->stackDepth - 1 - down);  // "element index"
  return *tristack_at(ts, ei);
}

static void
tristack_discardn(TriStack *ts, unsigned int n) {
  assert(n <= ts->stackDepth);

  ts->stackDepth -= n;

  //Adjust A region if we have popped down below it.
  if (ts->stackDepth < ts->abi)
    ts->abi = ts->stackDepth;
}

static void
tristack_push(TriStack *ts, Value v) {
  //CBINT FIXME how to check that there is room to push?  (old code didn't care)
  // We could make all stack edges be followed by a page which faults and
  // causes a reallocation of the stack.
  Value *astack = static_cast<Value*>(cb_at(thread_cb, ts->abo));
  astack[ts->stackDepth - ts->abi] = v;
  ++(ts->stackDepth);
}

static Value
tristack_pop(TriStack *ts) {
  assert(ts->stackDepth > 0);

  Value v = tristack_peek(ts, 0);
  tristack_discardn(ts, 1);
  return v;
}

void
tristack_print(TriStack *ts) {
  for (unsigned int i = 0; i < vm.tristack.stackDepth; ++i) {
    printf("%d%s[ ", i, tristack_regionname_at(&(vm.tristack), i));
    printValue(*tristack_at(&(vm.tristack), i));
    printf(" ] ");
  }
  printf("\n");
}

static void
triframes_reset(TriFrames *tf) {
  cb_offset_t new_offset;
  int ret;

  printf("DANDEBUG before triframes reset\n");
  ret = cb_region_memalign(&thread_cb,
                           &thread_region,
                           &new_offset,
                           cb_alignof(CallFrame),
                           sizeof(CallFrame) * FRAMES_MAX);
  printf("DANDEBUG after triframes reset\n");
  (void)ret;

  tf->abo = new_offset;
  tf->abi = 0;
  tf->bbo = CB_NULL;
  tf->bbi = 0;
  tf->cbo = CB_NULL;
  tf->cbi = 0;
  tf->frameCount = 0;
  tf->currentFrame = NULL;
}

static void
triframes_enterFrame(TriFrames *tf) {
  CallFrame *framesSubsection;

  assert(tf->frameCount >= tf->abi);

  framesSubsection = static_cast<CallFrame*>(cb_at(thread_cb, tf->abo));
  tf->currentFrame = &(framesSubsection[tf->frameCount - tf->abi]);
  ++(tf->frameCount);
}

static void
triframes_leaveFrame(TriFrames *tf) {
  CallFrame *newFrame;
  cb_offset_t offset;
  unsigned int parentFrameIndex;

  assert(tf->frameCount > 0);

  --(tf->frameCount);
  parentFrameIndex = tf->frameCount - 1;

  if (parentFrameIndex >= tf->abi) {
    // Parent frame we are returning to is already in the mutable A section.
    offset = tf->abo + (parentFrameIndex - tf->abi) * sizeof(CallFrame);
    tf->currentFrame = static_cast<CallFrame*>(cb_at(thread_cb, offset));
    return;
  }

  // Otherwise, parent frame we are returning to is in either the B or C
  // read-only sections. It must be copied to the mutable A section (will
  // have destination of abo), and abi adjustment must be made.
  if (parentFrameIndex < tf->bbi) {
    offset = tf->cbo + (parentFrameIndex - tf->cbi) * sizeof(CallFrame);
  } else {
    offset = tf->bbo + (parentFrameIndex - tf->bbi) * sizeof(CallFrame);
  }
  newFrame = static_cast<CallFrame*>(cb_at(thread_cb, tf->abo));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
  memcpy(newFrame,
         static_cast<CallFrame*>(cb_at(thread_cb, offset)),
         sizeof(CallFrame));
#pragma GCC diagnostic pop
  tf->abi = parentFrameIndex;
  tf->currentFrame = newFrame;
}

CallFrame*
triframes_at(TriFrames *tf, unsigned int index) {
  cb_offset_t offset;

  if (index >= tf->abi) {
    offset = tf->abo + (index - tf->abi) * sizeof(CallFrame);
  } else if (index < tf->bbi) {
    offset = tf->cbo + (index - tf->cbi) * sizeof(CallFrame);
  } else {
    offset = tf->bbo + (index - tf->bbi) * sizeof(CallFrame);
  }

  return static_cast<CallFrame*>(cb_at(thread_cb, offset));
}

static unsigned int
triframes_frameCount(TriFrames *tf) {
  return tf->frameCount;
}

static CallFrame*
triframes_currentFrame(TriFrames *tf) {
  assert(tf->currentFrame == triframes_at(tf, triframes_frameCount(tf) - 1));
  return tf->currentFrame;
}

VM vm; // [one]
//> Calls and Functions not-yet

static Value clockNative(int argCount, Value* args) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}
//< Calls and Functions not-yet
//> reset-stack
static void resetStack() {
  tristack_reset(&(vm.tristack));
//> Calls and Functions not-yet
  triframes_reset(&(vm.triframes));
//< Calls and Functions not-yet
//> Closures not-yet
  vm.openUpvalues = CB_NULL_OID;
//< Closures not-yet
}
//< reset-stack
//> Types of Values runtime-error
static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (unsigned int i = triframes_frameCount(&(vm.triframes)); i > 0; --i) {
    CallFrame *frame = triframes_at(&(vm.triframes), i - 1);

    OID<ObjFunction> function = frame->closure.clip()->function;
    // -1 because the IP is sitting on the next instruction to be
    // executed.
    size_t instruction = frame->ip - function.clip()->chunk.code.clp() - 1;
    fprintf(stderr, "[line %d] in ",
            function.clip()->chunk.lines.clp()[instruction]);
    if (function.clip()->name.is_nil()) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function.clip()->name.clip()->chars.clp());
    }
  }

  resetStack();
}
//< Types of Values runtime-error
//> Calls and Functions not-yet

static void defineNative(const char* name, NativeFn function) {
  OID<ObjString> nameOID = copyString(name, (int)strlen(name));
  Value nameVal = OBJ_VAL(nameOID.id());
  push(nameVal);  //Keep garbage collector happy. CBINT FIXME not needed

  OID<ObjNative> nativeOID = newNative(function);
  Value nativeVal = OBJ_VAL(nativeOID.id());
  push(nativeVal);  //Keep garbage collector happy.  CBINT FIXME not needed

  tableSet(&vm.globals, nameVal, nativeVal);
  pop();
  pop();
}
//< Calls and Functions not-yet

void initVM() {
//> call-reset-stack
  resetStack();
//< call-reset-stack
//> Strings init-objects-root
  vm.objects = CB_NULL_OID;
//< Strings init-objects-root
//> Garbage Collection not-yet
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = CB_NULL;
//< Garbage Collection not-yet
//> Global Variables init-globals

  objtable_init(&thread_objtable);

  initTable(&vm.globals, &clox_value_shallow_comparator, &clox_value_render);
//< Global Variables init-globals
//> Hash Tables init-strings
  initTable(&vm.strings, &clox_value_deep_comparator, &clox_value_render);
//< Hash Tables init-strings
//> Methods and Initializers not-yet

  vm.initString = copyString("init", 4);
//< Methods and Initializers not-yet
//> Calls and Functions not-yet

  defineNative("clock", clockNative);
//< Calls and Functions not-yet
}

void freeVM() {
//> Global Variables free-globals
  freeTable(&vm.globals);
//< Global Variables free-globals
//> Hash Tables free-strings
  freeTable(&vm.strings);
//< Hash Tables free-strings
//> Methods and Initializers not-yet
  vm.initString = CB_NULL_OID;
//< Methods and Initializers not-yet
//> Strings call-free-objects
  freeObjects();
//< Strings call-free-objects
}

//> push
void push(Value value) {
  tristack_push(&(vm.tristack), value);
}
//< push
//> pop
Value pop() {
  return tristack_pop(&(vm.tristack));
}
//< pop
//> Types of Values peek
static Value peek(int distance) {
  return tristack_peek(&(vm.tristack), distance);
}
//< Types of Values peek
/* Calls and Functions not-yet < Closures not-yet

static bool call(ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError("Expected %d arguments but got %d.",
        function->arity, argCount);
*/
//> Calls and Functions not-yet
//> Closures not-yet

static bool call(OID<ObjClosure> closure, int argCount) {
  if (argCount != closure.clip()->function.clip()->arity) {
    runtimeError("Expected %d arguments but got %d.",
        closure.clip()->function.clip()->arity, argCount);
//< Closures not-yet
    return false;
  }

  if (triframes_frameCount(&(vm.triframes)) == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  triframes_enterFrame(&(vm.triframes));
  CallFrame* frame = triframes_currentFrame(&(vm.triframes));
/* Calls and Functions not-yet < Closures not-yet
  frame->function = function;
  frame->ip = function->chunk.code;
*/
//> Closures not-yet
  frame->closure = closure;
  frame->ip = closure.clip()->function.clip()->chunk.code.clp();  //FIXME CBINT be careful around frame->ip + mutable copying of chunks + function returns.
//< Closures not-yet

  // +1 to include either the called function or the receiver.
  frame->slotsCount = argCount + 1;
  frame->slotsIndex = vm.tristack.stackDepth - frame->slotsCount;
  frame->slots = tristack_at(&(vm.tristack), frame->slotsIndex);
  assert(frame->slots >= cb_at(thread_cb, vm.tristack.abo));  //Slots must be contiguous in mutable section A.
  return true;
}

static bool instanceFieldGet(OID<ObjInstance> instance, Value key, Value *value) {
  const ObjInstance *instanceA;
  const ObjInstance *instanceB;
  const ObjInstance *instanceC;
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));

  instanceA = instance.clipA();
  if (instanceA) {
    ret = cb_bst_lookup(thread_cb, instanceA->fields_bst, &key_term, &value_term);
    if (ret == 0) goto done;
  }
  instanceB = instance.clipB();
  if (instanceB) {
    ret = cb_bst_lookup(thread_cb, instanceB->fields_bst, &key_term, &value_term);
    if (ret == 0) goto done;
  }
  instanceC = instance.clipC();
  if (instanceC) {
    ret = cb_bst_lookup(thread_cb, instanceC->fields_bst, &key_term, &value_term);
  }

done:
  if (ret != 0 || numToValue(cb_term_get_dbl(&value_term)).val == TOMBSTONE_VAL.val) {
    return false;
  }

  *value = numToValue(cb_term_get_dbl(&value_term));
  return true;
}

static bool instanceFieldSet(OID<ObjInstance> instance, Value key, Value value) {
  ObjInstance *instanceA;
  struct cb_term key_term;
  struct cb_term value_term;
  Value temp_value;
  bool already_exists;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));
  cb_term_set_dbl(&value_term, valueToNum(value));

  //CBINT FIXME would be nice to avoid this lookup by leveraging
  // cb_bst_insert()'s lookup.
  already_exists = instanceFieldGet(instance, key, &temp_value);

  instanceA = instance.mlip();

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      &(instanceA->fields_bst),
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  assert(ret == 0);

  //true if new key and we succeeded at inserting it.
  return (!already_exists && ret == 0);
}

static bool classMethodGet(OID<ObjClass> klass, Value key, Value *value) {
  const ObjClass *classA;
  const ObjClass *classB;
  const ObjClass *classC;
  struct cb_term key_term;
  struct cb_term value_term;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));

  classA = klass.clipA();
  if (classA) {
    ret = cb_bst_lookup(thread_cb, classA->methods_bst, &key_term, &value_term);
    if (ret == 0) goto done;
  }
  classB = klass.clipB();
  if (classB) {
    ret = cb_bst_lookup(thread_cb, classB->methods_bst, &key_term, &value_term);
    if (ret == 0) goto done;
  }
  classC = klass.clipC();
  if (classC) {
    ret = cb_bst_lookup(thread_cb, classC->methods_bst, &key_term, &value_term);
  }

done:
  if (ret != 0 || numToValue(cb_term_get_dbl(&value_term)).val == TOMBSTONE_VAL.val) {
    return false;
  }

  *value = numToValue(cb_term_get_dbl(&value_term));
  return true;
}

static bool classMethodSet(OID<ObjClass> klass, Value key, Value value) {
  ObjClass *classA;
  struct cb_term key_term;
  struct cb_term value_term;
  Value temp_value;
  bool already_exists;
  int ret;

  cb_term_set_dbl(&key_term, valueToNum(key));
  cb_term_set_dbl(&value_term, valueToNum(value));

  //CBINT FIXME would be nice to avoid this lookup by leveraging
  // cb_bst_insert()'s lookup.
  already_exists = classMethodGet(klass, key, &temp_value);

  classA = klass.mlip();

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      &(classA->methods_bst),
                      thread_cutoff_offset,
                      &key_term,
                      &value_term);
  assert(ret == 0);

  //true if new key and we succeeded at inserting it.
  return (!already_exists && ret == 0);
}

static int
bstTraversalAdd(const struct cb_term *key_term,
                const struct cb_term *value_term,
                void                 *closure)
{
  cb_offset_t *dest_bst = (cb_offset_t *)closure;
  int ret;

  (void)ret;

  ret = cb_bst_insert(&thread_cb,
                      &thread_region,
                      dest_bst,
                      thread_cutoff_offset,
                      key_term,
                      value_term);
  assert(ret == 0);
  return 0;
}

static void classMethodsAddAll(OID<ObjClass> subclass, OID<ObjClass> superclass) {
  int ret;

  (void)ret;

  ret = cb_bst_traverse(thread_cb,
                        superclass.clip()->methods_bst,
                        &bstTraversalAdd,
                        &(subclass.mlip()->methods_bst));
  assert(ret == 0);
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
//> Methods and Initializers not-yet
      case OBJ_BOUND_METHOD: {
        OID<ObjBoundMethod> bound = AS_BOUND_METHOD_OID(callee);

        // Replace the bound method with the receiver so it's in the
        // right slot when the method is called.
        Value* loc = tristack_at(&(vm.tristack), vm.tristack.stackDepth - (argCount + 1));
        assert(loc >= cb_at(thread_cb, vm.tristack.abo));  // Must be in the mutable section A.
        *loc = bound.clip()->receiver;
        return call(bound.clip()->method, argCount);
      }

//< Methods and Initializers not-yet
//> Classes and Instances not-yet
      case OBJ_CLASS: {
        OID<ObjClass> klass = AS_CLASS_OID(callee);

        // Create the instance.
        Value* loc = tristack_at(&(vm.tristack), vm.tristack.stackDepth - (argCount + 1));
        assert(loc >= cb_at(thread_cb, vm.tristack.abo));  // Must be in the mutable section A.
        *loc = OBJ_VAL(newInstance(klass).id());
//> Methods and Initializers not-yet
        // Call the initializer, if there is one.
        Value initializer;
        if (classMethodGet(klass, OBJ_VAL(vm.initString.id()), &initializer)) {
          return call(AS_CLOSURE_OID(initializer), argCount);
        } else if (argCount != 0) {
          runtimeError("Expected 0 arguments but got %d.", argCount);
          return false;
        }

//< Methods and Initializers not-yet
        return true;
      }
//< Classes and Instances not-yet
//> Closures not-yet

      case OBJ_CLOSURE:
        return call(AS_CLOSURE_OID(callee), argCount);

//< Closures not-yet
/* Calls and Functions not-yet < Closures not-yet
      case OBJ_FUNCTION:
        return call(AS_FUNCTION(callee), argCount);

*/
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        Value* loc = tristack_at(&(vm.tristack), vm.tristack.stackDepth - argCount);
        assert(loc >= cb_at(thread_cb, vm.tristack.abo));
        Value result = native(argCount, loc);
        tristack_discardn(&(vm.tristack), argCount + 1);
        push(result);
        return true;
      }

      default:
        // Do nothing.
        break;
    }
  }

  runtimeError("Can only call functions and classes.");
  return false;
}
//< Calls and Functions not-yet
//> Methods and Initializers not-yet

static bool invokeFromClass(OID<ObjClass> klass, OID<ObjString> name,
                            int argCount) {
  // Look for the method.
  Value method;
  if (!classMethodGet(klass, OBJ_VAL(name.id()), &method)) {
    runtimeError("Undefined property '%s'.", name.clip()->chars);
    return false;
  }

  return call(AS_CLOSURE_OID(method), argCount);
}

static bool invoke(OID<ObjString> name, int argCount) {
  Value receiver = peek(argCount);

  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  OID<ObjInstance> instance = AS_INSTANCE_OID(receiver);

  // First look for a field which may shadow a method.
  Value value;
  if (instanceFieldGet(instance, OBJ_VAL(name.id()), &value)) {
    Value *loc = tristack_at(&(vm.tristack), vm.tristack.stackDepth - argCount);
    assert(loc >= cb_at(thread_cb, vm.tristack.abo));
    *loc = value;
    return callValue(value, argCount);
  }

  return invokeFromClass(instance.clip()->klass, name, argCount);
}

static bool bindMethod(OID<ObjClass> klass, OID<ObjString> name) {
  Value method;
  if (!classMethodGet(klass, OBJ_VAL(name.id()), &method)) {
    runtimeError("Undefined property '%s'.", name.clip()->chars);
    return false;
  }

  OID<ObjBoundMethod> bound = newBoundMethod(peek(0), AS_CLOSURE_OID(method));
  pop(); // Instance.
  push(OBJ_VAL(bound.id()));
  return true;
}
//< Methods and Initializers not-yet
//> Closures not-yet

// Captures the local variable [local] into an [Upvalue]. If that local
// is already in an upvalue, the existing one is used. (This is
// important to ensure that multiple closures closing over the same
// variable actually see the same variable.) Otherwise, it creates a
// new open upvalue and adds it to the VM's list of upvalues.
static OID<ObjUpvalue> captureUpvalue(unsigned int localStackIndex) {  //CBINT FIXME will need to be offset.
  // If there are no open upvalues at all, we must need a new one.
  if (vm.openUpvalues.is_nil()) {
    vm.openUpvalues = newUpvalue(localStackIndex);
    return vm.openUpvalues;
  }

  OID<ObjUpvalue> prevUpvalue = CB_NULL_OID;
  OID<ObjUpvalue> upvalue = vm.openUpvalues;

  // Walk towards the bottom of the stack until we find a previously
  // existing upvalue or reach where it should be.
  while (!upvalue.is_nil() && upvalue.clip()->valueStackIndex > (int)localStackIndex) {
    prevUpvalue = upvalue;
    upvalue = upvalue.clip()->next;
  }

  // If we found it, reuse it.
  if (!upvalue.is_nil() && upvalue.clip()->valueStackIndex == (int)localStackIndex) return upvalue;

  // We walked past the local on the stack, so there must not be an
  // upvalue for it already. Make a new one and link it in in the right
  // place to keep the list sorted.
  OID<ObjUpvalue> createdUpvalue = newUpvalue(localStackIndex);
  createdUpvalue.mlip()->next = upvalue;

  if (prevUpvalue.is_nil()) {
    // The new one is the first one in the list.
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue.mlip()->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(unsigned int lastStackIndex) {
  //NOTE: The pointer comparison of vm.openValues.lp()->valueStackIndex vs.
  // 'lastStackIndex' works because lastStackIndex must exist within the stack.
  // The intent of this function is to take every upvalue of stack positions
  // greater-than-or-equal-to in index than 'lastStackIndex' and turn them into
  // "closed" upvalues, which hold a value in their own local storage instead
  // of referring to stack indices.  The 'lastStackIndex' argument which is
  // passed is the index of the first slot of a frame which is being exited (in
  // the case of an OP_RETURN for leaving functions), or the index of the
  // last element of the stack (in the case of OP_CLOSE_UPVALUE for leaving
  // scopes).

  while (!vm.openUpvalues.is_nil() &&
         vm.openUpvalues.clip()->valueStackIndex >= (int)lastStackIndex) {
    OID<ObjUpvalue> upvalue = vm.openUpvalues;

    // Move the value into the upvalue itself and point the upvalue to
    // it.
    upvalue.mlip()->closed = *tristack_at(&(vm.tristack), upvalue.clip()->valueStackIndex);
    upvalue.mlip()->valueStackIndex = -1;

    // Pop it off the open upvalue list.
    vm.openUpvalues = upvalue.clip()->next;
  }
}
//< Closures not-yet
//> Methods and Initializers not-yet

static void defineMethod(OID<ObjString> name) {
  Value method = peek(0);
  OID<ObjClass> klass = AS_CLASS_OID(peek(1));
  classMethodSet(klass, OBJ_VAL(name.id()), method);
  pop();
}
//< Methods and Initializers not-yet
/* Classes and Instances not-yet < Superclasses not-yet

static void createClass(ObjString* name) {
  ObjClass* klass = newClass(name);
*/
//> Classes and Instances not-yet
//> Superclasses not-yet

static void createClass(OID<ObjString> name, OID<ObjClass> superclass) {
  OID<ObjClass> klass = newClass(name, superclass);
//< Superclasses not-yet
  push(OBJ_VAL(klass.id()));
//> Superclasses not-yet

  // Inherit methods.
  if (!superclass.is_nil()) {
    classMethodsAddAll(klass, superclass);
  }
//< Superclasses not-yet
}
//< Classes and Instances not-yet
//> Types of Values is-falsey
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}
//< Types of Values is-falsey
//> Strings concatenate
static void concatenate() {
/* Strings concatenate < Garbage Collection not-yet
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());
*/
//> Garbage Collection not-yet
  OID<ObjString> b = AS_STRING_OID(peek(0));
  OID<ObjString> a = AS_STRING_OID(peek(1));
//< Garbage Collection not-yet

  int length = a.clip()->length + b.clip()->length;
  CBO<char> /*char[]*/ chars = ALLOCATE(char, length + 1);
  memcpy(chars.mlp(), a.clip()->chars.clp(), a.clip()->length);
  memcpy(chars.mlp() + a.clip()->length, b.clip()->chars.clp(), b.clip()->length);
  chars.mlp()[length] = '\0';

  OID<ObjString> result = takeString(chars, length);
//> Garbage Collection not-yet
  pop();
  pop();
//< Garbage Collection not-yet
  push(OBJ_VAL(result.id()));
}
//< Strings concatenate
//> run
static InterpretResult run() {
  CallFrame* frame = triframes_currentFrame(&(vm.triframes));

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
    (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() \
    (frame->closure.clip()->function.clip()->chunk.constants.values.clp()[READ_BYTE()])
#define READ_STRING() AS_STRING_OID(READ_CONSTANT())

//> Types of Values binary-op
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)
//< Types of Values binary-op

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    //printf("          ");
    printf("DANDEBUGSTACK  ");
    tristack_print(&(vm.tristack));

    disassembleInstruction(&frame->closure.clip()->function.clip()->chunk,
        (int)(frame->ip - frame->closure.clip()->function.clip()->chunk.code.clp()));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
//> op-constant
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
/* A Virtual Machine op-constant < A Virtual Machine push-constant
        printValue(constant);
        printf("\n");
*/
//> push-constant
        push(constant);
//< push-constant
        break;
      }
//< op-constant
//> Types of Values interpret-literals
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
//< Types of Values interpret-literals
//> Global Variables interpret-pop
      case OP_POP: pop(); break;
//< Global Variables interpret-pop
//> Local Variables not-yet

      case OP_GET_LOCAL: {
        uint8_t slot = READ_BYTE();
/* Local Variables not-yet < Calls and Functions not-yet
        push(vm.stack[slot]);
*/
//> Calls and Functions not-yet
        push(frame->slots[slot]);
//< Calls and Functions not-yet
        break;
      }

      case OP_SET_LOCAL: {
        uint8_t slot = READ_BYTE();
/* Local Variables not-yet < Calls and Functions not-yet
        vm.stack[slot] = peek(0);
*/
//> Calls and Functions not-yet
        frame->slots[slot] = peek(0);
//< Calls and Functions not-yet
        break;
      }
//< Local Variables not-yet
//> Global Variables interpret-get-global

      case OP_GET_GLOBAL: {
        OID<ObjString> name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name.id()), &value)) {
          runtimeError("Undefined variable '%s'.", name.clip()->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
//< Global Variables interpret-get-global
//> Global Variables interpret-define-global

      case OP_DEFINE_GLOBAL: {
        OID<ObjString> name = READ_STRING();
        tableSet(&vm.globals, OBJ_VAL(name.id()), peek(0));
        pop();
        break;
      }
//< Global Variables interpret-define-global
//> Global Variables interpret-set-global

      case OP_SET_GLOBAL: {
        OID<ObjString> name = READ_STRING();
        if (tableSet(&vm.globals, OBJ_VAL(name.id()), peek(0))) {
          runtimeError("Undefined variable '%s'.", name.clip()->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
//< Global Variables interpret-set-global
//> Closures not-yet

      case OP_GET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        const ObjUpvalue* upvalue = frame->closure.clip()->upvalues.clp()[slot].clip();
        if (upvalue->valueStackIndex == -1) {
          push(upvalue->closed);
        } else {
          push(*tristack_at(&(vm.tristack), upvalue->valueStackIndex));
        }
        break;
      }

      case OP_SET_UPVALUE: {
        uint8_t slot = READ_BYTE();
        ObjUpvalue* upvalue = frame->closure.mlip()->upvalues.mlp()[slot].mlip();
        if (upvalue->valueStackIndex == -1) {
          upvalue->closed = peek(0);
        } else {
          *tristack_at(&(vm.tristack), upvalue->valueStackIndex) = peek(0);
        }
        break;
      }
//< Closures not-yet
//> Classes and Instances not-yet

      case OP_GET_PROPERTY: {
        if (!IS_INSTANCE(peek(0))) {
          runtimeError("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        OID<ObjInstance> instance = AS_INSTANCE_OID(peek(0));
        OID<ObjString> name = READ_STRING();
        Value value;
        if (instanceFieldGet(instance, OBJ_VAL(name.id()), &value)) {
          pop(); // Instance.
          push(value);
          break;
        }

/* Classes and Instances not-yet < Methods and Initializers not-yet
        runtimeError("Undefined property '%s'.", name->chars);
        return INTERPRET_RUNTIME_ERROR;
*/
//> Methods and Initializers not-yet
        if (!bindMethod(instance.clip()->klass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
//< Methods and Initializers not-yet
      }

      case OP_SET_PROPERTY: {
        if (!IS_INSTANCE(peek(1))) {
          runtimeError("Only instances have fields.");
          return INTERPRET_RUNTIME_ERROR;
        }

        OID<ObjInstance> instance = AS_INSTANCE_OID(peek(1));
        instanceFieldSet(instance, OBJ_VAL(READ_STRING().id()), peek(0));
        Value value = pop();
        pop();
        push(value);
        break;
      }
//< Classes and Instances not-yet
//> Superclasses not-yet

      case OP_GET_SUPER: {
        OID<ObjString> name = READ_STRING();
        OID<ObjClass> superclass = AS_CLASS_OID(pop());
        if (!bindMethod(superclass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
//< Superclasses not-yet
//> Types of Values interpret-equal

      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
        
//< Types of Values interpret-equal
//> Types of Values interpret-comparison
      case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
//< Types of Values interpret-comparison
/* A Virtual Machine op-binary < Types of Values op-arithmetic
      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;
*/
/* A Virtual Machine op-negate < Types of Values op-negate
      case OP_NEGATE:   push(-pop()); break;
*/
/* Types of Values op-arithmetic < Strings add-strings
      case OP_ADD:      BINARY_OP(NUMBER_VAL, +); break;
*/
//> Strings add-strings
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
//< Strings add-strings
//> Types of Values op-arithmetic
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
//< Types of Values op-arithmetic
//> Types of Values op-not
      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
//< Types of Values op-not
//> Types of Values op-negate
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }

        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
//< Types of Values op-negate
//> Global Variables interpret-print

      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }

//< Global Variables interpret-print
//> Jumping Forward and Back not-yet
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }

      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) frame->ip += offset;
        break;
      }

      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }
//< Jumping Forward and Back not-yet
//> Calls and Functions not-yet

      case OP_CALL_0:
      case OP_CALL_1:
      case OP_CALL_2:
      case OP_CALL_3:
      case OP_CALL_4:
      case OP_CALL_5:
      case OP_CALL_6:
      case OP_CALL_7:
      case OP_CALL_8: {
        int argCount = instruction - OP_CALL_0;
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = triframes_currentFrame(&(vm.triframes));
        break;
      }
//< Calls and Functions not-yet
//> Methods and Initializers not-yet

      case OP_INVOKE_0:
      case OP_INVOKE_1:
      case OP_INVOKE_2:
      case OP_INVOKE_3:
      case OP_INVOKE_4:
      case OP_INVOKE_5:
      case OP_INVOKE_6:
      case OP_INVOKE_7:
      case OP_INVOKE_8: {
        OID<ObjString> method = READ_STRING();
        int argCount = instruction - OP_INVOKE_0;
        if (!invoke(method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = triframes_currentFrame(&(vm.triframes));
        break;
      }
//< Methods and Initializers not-yet
//> Superclasses not-yet

      case OP_SUPER_0:
      case OP_SUPER_1:
      case OP_SUPER_2:
      case OP_SUPER_3:
      case OP_SUPER_4:
      case OP_SUPER_5:
      case OP_SUPER_6:
      case OP_SUPER_7:
      case OP_SUPER_8: {
        OID<ObjString> method = READ_STRING();
        int argCount = instruction - OP_SUPER_0;
        OID<ObjClass> superclass = AS_CLASS_OID(pop());
        if (!invokeFromClass(superclass, method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        frame = triframes_currentFrame(&(vm.triframes));
        break;
      }
//< Superclasses not-yet
//> Closures not-yet

      case OP_CLOSURE: {
        OID<ObjFunction> function = AS_FUNCTION_OID(READ_CONSTANT());

        // Create the closure and push it on the stack before creating
        // upvalues so that it doesn't get collected.
        OID<ObjClosure> closure = newClosure(function);
        push(OBJ_VAL(closure.id()));

        // Capture upvalues.
        for (int i = 0; i < closure.clip()->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();  // an index within the present frame's slots
          if (isLocal) {
            // Make an new upvalue to close over the parent's local
            // variable.
            closure.mlip()->upvalues.mlp()[i] = captureUpvalue(frame->slotsIndex + index);
          } else {
            // Use the same upvalue as the current call frame.
            closure.mlip()->upvalues.mlp()[i] = frame->closure.mlip()->upvalues.mlp()[index];
          }
        }

        break;
      }

      case OP_CLOSE_UPVALUE: {
        closeUpvalues(vm.tristack.stackDepth - 1);
        pop();
        break;
      }

//< Closures not-yet
      case OP_RETURN: {
        printf("DANDEBUG from frame's slotsIndex: %d, slotsCount: %d\n", frame->slotsIndex, frame->slotsCount);
        Value result = pop();

        // Close any upvalues still in scope.
        closeUpvalues(frame->slotsIndex);

        triframes_leaveFrame(&(vm.triframes));  // NOTE: does not yet update our local variable 'frame'.
        if (triframes_frameCount(&(vm.triframes)) == 0) return INTERPRET_OK;

        //NOTE: The purpose of this section is to move "up" (read: lower in
        // index) the stack. If we've returned to a position which is at a
        // lower index than where abi begins, we must move everything which
        // exists higher than this index into the mutable section A, and shift
        // abi to the target index.  This will maintain the invariant that
        // our function arguments are always contiguous in the mutable
        // section A.
        // The frame we're leaving should have always only existed in the
        // mutable A region.
        //assert(frame->slotsIndex >= vm.tristack.abi);  //NOT TRUE, A region may be empty due to GC.
        // Shorten the stack.
        vm.tristack.stackDepth = frame->slotsIndex;  //slotsIndex being one-past the highest index of new shorter stack
        push(result);

        // Now move to the outer frame, but if we've returned into the B or C
        // non-mutable regions of the stack, adjust the tristack to shift the
        // outer frame's slots into the A region.
        frame = triframes_currentFrame(&(vm.triframes));
        printf("DANDEBUG to frame's slotsIndex: %d, slotsCount: %d\n", frame->slotsIndex, frame->slotsCount);
        if (frame->slotsIndex < vm.tristack.abi) {
          vm.tristack.abi = frame->slotsIndex;
          memcpy(tristack_at(&(vm.tristack), vm.tristack.abi),
                 tristack_at_bc(&(vm.tristack), frame->slotsIndex),
                 frame->slotsCount * sizeof(Value));
          frame->slots = tristack_at(&(vm.tristack), frame->slotsIndex);  //re-derive.
        }
        assert(frame->slots == tristack_at(&(vm.tristack), frame->slotsIndex));
        assert(frame->slots >= cb_at(thread_cb, vm.tristack.abo));  //Must be contiguous, and in mutable section A.  FIXME a pointer comparison doesn't guarantee this due to ring wrap-around
        assert(frame->slotsIndex >= vm.tristack.abi);

        printf("OP_RETURN COMPLETE\n");
        break;
//< Calls and Functions not-yet
      }
//> Classes and Instances not-yet

      case OP_CLASS:
/* Classes and Instances not-yet < Superclasses not-yet
        createClass(READ_STRING());
*/
//> Superclasses not-yet
        createClass(READ_STRING(), CB_NULL_OID);
//< Superclasses not-yet
        break;
//< Classes and Instances not-yet
//> Superclasses not-yet

      case OP_SUBCLASS: {
        Value superclass = peek(0);
        if (!IS_CLASS(superclass)) {
          runtimeError("Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }

        createClass(READ_STRING(), AS_CLASS_OID(superclass));
        break;
      }
//< Superclasses not-yet
//> Methods and Initializers not-yet

      case OP_METHOD:
        defineMethod(READ_STRING());
        break;
//< Methods and Initializers not-yet
    }
  }

#undef READ_BYTE
//> Jumping Forward and Back not-yet
#undef READ_SHORT
//< Jumping Forward and Back not-yet
//> undef-read-constant
#undef READ_CONSTANT
//< undef-read-constant
//> Global Variables undef-read-string
#undef READ_STRING
//< Global Variables undef-read-string
//> undef-binary-op
#undef BINARY_OP
//< undef-binary-op
}
//< run
//> interpret
//> Scanning on Demand vm-interpret-c
InterpretResult interpret(const char* source) {
/* Scanning on Demand omit < Compiling Expressions interpret-chunk
  // Hack to avoid unused function error. run() is not used in the
  // scanning chapter.
  if (false) run();
*/
/* Scanning on Demand vm-interpret-c < Compiling Expressions interpret-chunk
  compile(source);
  return INTERPRET_OK;
*/
//> Calls and Functions not-yet
  OID<ObjFunction> function = compile(source);
  if (function.is_nil()) return INTERPRET_COMPILE_ERROR;

//< Calls and Functions not-yet
/* Calls and Functions not-yet < Closures not-yet
  callValue(OBJ_VAL(function), 0);
*/
//> Garbage Collection not-yet
  push(OBJ_VAL(function.id()));
//< Garbage Collection not-yet
//> Closures not-yet
  OID<ObjClosure> closure = newClosure(function);
//< Closures not-yet
//> Garbage Collection not-yet
  pop();
//< Garbage Collection not-yet
//> Closures not-yet

  //BUGFIX: without this, upstream code derived frames[0]->slots outside of stack.
  //  This was harmless, but trips our careful assertions.
  push(OBJ_VAL(closure.id()));

  callValue(OBJ_VAL(closure.id()), 0);

//< Closures not-yet
//< Scanning on Demand vm-interpret-c
//> Compiling Expressions interpret-chunk
  
  InterpretResult result = run();
/* Compiling Expressions interpret-chunk < Calls and Functions not-yet

  freeChunk(&chunk);
*/
  return result;
//< Compiling Expressions interpret-chunk
}
//< interpret
