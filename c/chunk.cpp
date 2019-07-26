#include <stdlib.h>

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
  initChunk(f);
}

void writeChunk(OID<Obj> f, uint8_t byte, int line) {
  ObjFunction *fun = (ObjFunction *)f.mlip();
  Chunk *chunk = &(fun->chunk);

  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;
    chunk->capacity = GROW_CAPACITY(oldCapacity);
    chunk->code = GROW_ARRAY(chunk->code.co(), uint8_t,
        oldCapacity, chunk->capacity);
    chunk->lines = GROW_ARRAY(chunk->lines.co(), int,
        oldCapacity, chunk->capacity);
  }

  //Rederive chunk, in case intervening GC caused it to become read-only.
  fun = (ObjFunction *)f.mlip();
  chunk = &(fun->chunk);

  chunk->code.mlp()[chunk->count] = byte;
  chunk->lines.mlp()[chunk->count] = line;
  chunk->count++;
}

int addConstant(OID<Obj> f, Value value) {
  ObjFunction *fun = (ObjFunction *)f.mlip();
  Chunk *chunk = &(fun->chunk);

  push(value);  //Protect value from GC

  if (chunk->constants.capacity < chunk->constants.count + 1) {
    int oldCapacity = chunk->constants.capacity;
    chunk->constants.capacity = GROW_CAPACITY(oldCapacity);
    chunk->constants.values = GROW_ARRAY(chunk->constants.values.co(), Value,
                               oldCapacity, chunk->constants.capacity);
  }

  //Rederive chunk, in case intervening GC caused it to become read-only.
  fun = (ObjFunction *)f.mlip();
  chunk = &(fun->chunk);

  chunk->constants.values.mlp()[chunk->constants.count] = value;
  chunk->constants.count++;

  pop();

  return chunk->constants.count - 1;
}

