#include <stdlib.h>

#include <cb_bst.h>

#include "chunk.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

void initChunk(OID<Obj> f) {
  ObjFunction *mfun = (ObjFunction *)f.mlip();
  Chunk *mchunk = &(mfun->chunk);

  mchunk->count = 0;
  mchunk->capacity = 0;
  mchunk->code = CB_NULL;
  mchunk->lines = CB_NULL;
  mchunk->constants.values = CB_NULL;
  mchunk->constants.capacity = 0;
  mchunk->constants.count = 0;
}

void freeChunk(OID<Obj> f) {
  PIN_SCOPE;

  ObjFunction *cfun = (ObjFunction *)f.clip();
  Chunk *cchunk = &(cfun->chunk);

  FREE_ARRAY(uint8_t, cchunk->code.co(), cchunk->capacity);
  FREE_ARRAY(int, cchunk->lines.co(), cchunk->capacity);
  FREE_ARRAY(Value, cchunk->constants.values.co(), cchunk->constants.capacity);

  ObjFunction *mfun = (ObjFunction *)f.mlip();
  Chunk *mchunk = &(mfun->chunk);

  mchunk->count = 0;
  mchunk->capacity = 0;
  mchunk->code = CB_NULL;
  mchunk->lines = CB_NULL;
  mchunk->constants.values = CB_NULL;
  mchunk->constants.capacity = 0;
  mchunk->constants.count = 0;
}

void writeChunk(OID<Obj> f, uint8_t byte, int line) {
  PIN_SCOPE;
  const ObjFunction *cfun = (const ObjFunction *)f.clip();
  const Chunk *cchunk = &(cfun->chunk);

  int newCapacity = cchunk->capacity;
  CBO<uint8_t> newCode;
  CBO<int > newLines;
  bool hasNewArrays = false;

  if (cchunk->capacity < cchunk->count + 1) {
    int oldCapacity = cchunk->capacity;
    newCapacity = GROW_CAPACITY(oldCapacity);
    newCode = GROW_ARRAY(cchunk->code.co(), uint8_t,
        oldCapacity, newCapacity);
    newLines = GROW_ARRAY(cchunk->lines.co(), int,
        oldCapacity, newCapacity);
    hasNewArrays = true;

    //NOTE: Because this chunk extension is done to an chunk already present
    // in the objtable (it is held by an ObjFunction, which is held in the
    // objtable), we must manually inform the objtable of this independent
    // mutation of external size.
    cb_bst_external_size_adjust(thread_cb,
                                thread_objtable.root_a,
                                (newCapacity - oldCapacity) * (sizeof(uint8_t) + sizeof(int)));
  }

  ObjFunction *mfun = (ObjFunction *)f.mlip();
  Chunk *mchunk = &(mfun->chunk);

  mchunk->capacity = newCapacity;
  if (hasNewArrays) {
    mchunk->code = newCode;
    mchunk->lines = newLines;
  }
  mchunk->code.mlp()[mchunk->count] = byte;
  mchunk->lines.mlp()[mchunk->count] = line;
  mchunk->count++;
}

int addConstant(OID<Obj> f, Value value) {
  PIN_SCOPE;
  const ObjFunction *cfun = (const ObjFunction *)f.clip();
  const Chunk *cchunk = &(cfun->chunk);

  push(value);  //Protect value from GC

  int newCapacity = cchunk->constants.capacity;
  CBO<Value> newValues = cchunk->constants.values;
  bool hasNewArray = false;

  if (cchunk->constants.capacity < cchunk->constants.count + 1) {
    int oldCapacity = cchunk->constants.capacity;
    newCapacity = GROW_CAPACITY(oldCapacity);
    newValues = GROW_ARRAY(cchunk->constants.values.co(), Value,
                           oldCapacity, newCapacity);
    hasNewArray = true;

    //NOTE: Because this constants extension is done to an chunk already present
    // in the objtable (it is held by an ObjFunction, which is held in the
    // objtable), we must manually inform the objtable of this independent
    // mutation of external size.
    cb_bst_external_size_adjust(thread_cb,
                                thread_objtable.root_a,
                                (newCapacity - oldCapacity) * sizeof(Value));
  }

  ObjFunction *mfun = (ObjFunction *)f.mlip();
  Chunk *mchunk = &(mfun->chunk);

  mchunk->constants.capacity = newCapacity;
  if (hasNewArray)
    mchunk->constants.values = newValues;
  mchunk->constants.values.mlp()[mchunk->constants.count] = value;
  mchunk->constants.count++;

  pop();

  return mchunk->constants.count - 1;
}

