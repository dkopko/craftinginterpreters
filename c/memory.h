//> Chunks of Bytecode memory-h
#ifndef clox_memory_h
#define clox_memory_h

#include "cb_integration.h"
#include "object.h"

#define ALLOCATE(type, count) \
    logged_allocate(#type, sizeof(type), (count), cb_alignof(type))

#define FREE(type, objid) \
    logged_free(#type, objid, sizeof(type), cb_alignof(type))

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(previous, type, oldCount, count) \
    logged_grow_array(#type, previous, sizeof(type), (oldCount), (count), cb_alignof(type))

#define FREE_ARRAY(type, previous, oldCount) \
    logged_free_array(#type, previous, sizeof(type), (oldCount), cb_alignof(type))


ObjID reallocate(ObjID previous, size_t oldSize, size_t newSize, size_t alignment, bool suppress_gc);


extern inline ObjID
logged_allocate(const char *typeName, size_t typeSize, size_t count, size_t alignment)
{
  ObjID retval = reallocate(CB_NULL_OID, 0, typeSize * count, alignment, false);

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
logged_free(const char *typeName, ObjID previous, size_t typeSize, size_t alignment)
{
  ObjID retval = reallocate(previous, typeSize, 0, alignment, false);

#ifdef DEBUG_TRACE_GC
  printf("#%ju %s object freed (-%zd bytes)\n",
         (uintmax_t)previous.id,
         typeName,
         typeSize);
#endif

  assert(retval.id == CB_NULL_OID.id);
  return retval;
}

extern inline ObjID
logged_grow_array(const char *typeName, ObjID previous, size_t typeSize, size_t oldCount, size_t count, size_t alignment)
{
  ObjID retval = reallocate(previous, typeSize * oldCount, typeSize * count, alignment, false);

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

extern inline ObjID
logged_free_array(const char *typeName, ObjID previous, size_t typeSize, size_t oldCount, size_t alignment)
{
  ObjID retval = reallocate(previous, typeSize * oldCount, 0, alignment, false);

#ifdef DEBUG_TRACE_GC
  printf("#%ju %s[%zd] array freed (-%zd bytes)\n",
         (uintmax_t)previous.id,
         typeName,
         oldCount,
         typeSize * oldCount);
#endif

  assert(retval.id == CB_NULL_OID.id);
  return retval;
}


bool isWhite(Value value);
void grayObject(OID<Obj> objectOID);
void grayValue(Value value);
void collectGarbage();
void freeObjects();

#endif
