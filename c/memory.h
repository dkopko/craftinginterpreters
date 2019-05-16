//> Chunks of Bytecode memory-h
#ifndef clox_memory_h
#define clox_memory_h

#include "cb_integration.h"
//> Strings memory-include-object
#include "object.h"

//< Strings memory-include-object
//> Strings allocate
    //reallocate(CB_NULL_OID, 0, sizeof(type) * (count), cb_alignof(type))
#define ALLOCATE(type, count) \
    logged_allocate(#type, sizeof(type), (count), cb_alignof(type))
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
    logged_grow_array(#type, previous, sizeof(type), (oldCount), \
        (count), cb_alignof(type))
//> free-array

#define FREE_ARRAY(type, pointer, oldCount) \
    do {} while (0)
    //reallocate(pointer, sizeof(type) * (oldCount), 0)
//< free-array

ObjID reallocate(ObjID previous, size_t oldSize, size_t newSize, size_t alignment);
//< grow-array

extern inline ObjID
logged_allocate(const char *typeName, size_t typeSize, size_t count, size_t alignment)
{
  ObjID retval = reallocate(CB_NULL_OID, 0, typeSize * count, alignment);

#ifdef DEBUG_TRACE_GC
  printf("#%ju %s[%zd] array allocated (%zd bytes)\n",
         (uintmax_t)retval.id,
         typeName,
         count,
         typeSize * count);
#endif

  return retval;
}

extern inline ObjID
logged_grow_array(const char *typeName, ObjID previous, size_t typeSize, size_t oldCount, size_t count, size_t alignment)
{
  ObjID retval = reallocate(previous, typeSize * oldCount, typeSize * count, alignment);

#ifdef DEBUG_TRACE_GC
  printf("#%ju %s[%zd] array (%zd bytes) resized to #%ju %s[%zd] array (%zd bytes)\n",
         (uintmax_t)previous.id,
         typeName,
         oldCount,
         typeSize * oldCount,
         (uintmax_t)retval.id,
         typeName,
         count,
         typeSize * count);
#endif

  return retval;
}

//> Garbage Collection not-yet

void grayObject(Obj* object);
void grayValue(Value value);
void collectGarbage();
//< Garbage Collection not-yet
//> Strings free-objects-h
void freeObjects();
//< Strings free-objects-h

#endif
