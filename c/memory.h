//> Chunks of Bytecode memory-h
#ifndef clox_memory_h
#define clox_memory_h

#include "cb_integration.h"
//> Strings memory-include-object
#include "object.h"

//< Strings memory-include-object
//> Strings allocate
#define ALLOCATE(type, count) \
    reallocate(CB_NULL, 0, sizeof(type) * (count), cb_alignof(type))
//> free

#define FREE(type, pointer) \
    do {} while (0)
    // reallocate(pointer, sizeof(type), 0)
//< free

//< Strings allocate
#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)
//> grow-array

#define GROW_ARRAY(previous, type, oldCount, count) \
    reallocate(previous, sizeof(type) * (oldCount), \
        sizeof(type) * (count), cb_alignof(type))
//> free-array

#define FREE_ARRAY(type, pointer, oldCount) \
    do {} while (0)
    //reallocate(pointer, sizeof(type) * (oldCount), 0)
//< free-array

cb_offset_t reallocate(cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment);
//< grow-array
//> Garbage Collection not-yet

void grayObject(Obj* object);
void grayValue(Value value);
void collectGarbage();
//< Garbage Collection not-yet
//> Strings free-objects-h
void freeObjects();
//< Strings free-objects-h

#endif
