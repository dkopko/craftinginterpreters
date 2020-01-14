#include <stdlib.h>

#include <cb_bst.h>

#include "chunk.h"
#include "memory.h"
#include "value.h"
#include "vm.h"

void initChunk(OID<Obj> f) {
  ObjFunction *fun = (ObjFunction *)f.mlip();
  Chunk *chunk = &(fun->chunk);

  chunk->count = 0;
  chunk->capacity = 0;
  chunk->code = CB_NULL;
  chunk->lines = CB_NULL;
  chunk->constants.values = CB_NULL;
  chunk->constants.capacity = 0;
  chunk->constants.count = 0;
}

void freeChunk(OID<Obj> f) {
  ObjFunction *fun = (ObjFunction *)f.mlip();
  Chunk *chunk = &(fun->chunk);

  FREE_ARRAY(uint8_t, chunk->code.co(), chunk->capacity);
  FREE_ARRAY(int, chunk->lines.co(), chunk->capacity);
  FREE_ARRAY(Value, chunk->constants.values.co(), chunk->constants.capacity);
  chunk->constants.values = CB_NULL;
  chunk->constants.capacity = 0;
  chunk->constants.count = 0;
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

//FIXME the following code shouldn't work??  The "correct" formulation isn't
// working though.  The issue: there are 3 allocations here, which can have
// bad interactions due to GC at each allocation.  Yet taking a const * of
// the function initially and only making a mutable copy at the end is not
// working.
int addConstant(OID<Obj> f, Value value) {
  PIN_SCOPE;
  ObjFunction *fun = (ObjFunction *)f.mlip();
  Chunk *chunk = &(fun->chunk);

  push(value);  //Protect value from GC

  int newCapacity = chunk->constants.capacity;
  if (chunk->constants.capacity < chunk->constants.count + 1) {
    int oldCapacity = chunk->constants.capacity;
    newCapacity = GROW_CAPACITY(oldCapacity);
    chunk->constants.capacity = newCapacity;
    CBO<Value> vals = GROW_ARRAY(chunk->constants.values.co(), Value,
                               oldCapacity, newCapacity);

    //Rederive chunk, in case intervening GC caused it to become read-only.
    fun = (ObjFunction *)f.mlip();
    chunk = &(fun->chunk);

    //FIXME this GROW_ARRAY allocation may invalidate fun and chunk.  They will need to be rederived.
    chunk->constants.values = vals;


    //NOTE: Because this constants extension is done to an chunk already present
    // in the objtable (it is held by an ObjFunction, which is held in the
    // objtable), we must manually inform the objtable of this independent
    // mutation of external size.
    cb_bst_external_size_adjust(thread_cb,
                                thread_objtable.root_a,
                                (newCapacity - oldCapacity) * sizeof(Value));
  }


  chunk->constants.values.mlp()[chunk->constants.count] = value;
  chunk->constants.count++;

  pop();

  for (int i = 0; i < chunk->constants.count; ++i) {
    printf("DANDEBUG constants: %d:", i);
    printValue(chunk->constants.values.mlp()[i]);
    printf(" ");
  }
  printf("\n");

  return chunk->constants.count - 1;
}

