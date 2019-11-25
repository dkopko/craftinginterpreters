//> Chunks of Bytecode memory-h
#ifndef clox_memory_h
#define clox_memory_h

#include "cb_integration.h"
#include "object.h"


enum {
  GC_PHASE_NORMAL_EXEC,
  GC_PHASE_ACTIVE_GC
};

extern int gc_phase;


#define ALLOCATE(type, count) \
    logged_allocate(#type, sizeof(type), (count), cb_alignof(type))

#define FREE(type, offset) \
    logged_free(#type, offset, sizeof(type), cb_alignof(type))

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(previous, type, oldCount, count) \
    logged_grow_array(#type, previous, sizeof(type), (oldCount), (count), cb_alignof(type), false)

#define GROW_ARRAY_NOGC(previous, type, oldCount, count) \
    logged_grow_array(#type, previous, sizeof(type), (oldCount), (count), cb_alignof(type), true)

#define FREE_ARRAY(type, previous, oldCount) \
    logged_free_array(#type, previous, sizeof(type), (oldCount), cb_alignof(type))


cb_offset_t reallocate(cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment, bool suppress_gc);


extern inline cb_offset_t
logged_allocate(const char *typeName, size_t typeSize, size_t count, size_t alignment)
{
  cb_offset_t retval = reallocate(CB_NULL, 0, typeSize * count, alignment, false);

#ifdef DEBUG_TRACE_GC
  printf("@%ju %s[%zd] array allocated (%zd bytes)\n",
         (uintmax_t)retval,
         typeName,
         count,
         typeSize * count);
#endif

  return retval;
}

extern inline cb_offset_t
logged_free(const char *typeName, cb_offset_t previous, size_t typeSize, size_t alignment)
{
  cb_offset_t retval = reallocate(previous, typeSize, 0, alignment, false);

#ifdef DEBUG_TRACE_GC
  printf("@%ju %s object freed (-%zd bytes)\n",
         (uintmax_t)previous,
         typeName,
         typeSize);
#endif

  assert(retval == CB_NULL);
  return retval;
}

extern inline cb_offset_t
logged_grow_array(const char *typeName, cb_offset_t previous, size_t typeSize, size_t oldCount, size_t count, size_t alignment, bool suppress_gc)
{
  cb_offset_t retval = reallocate(previous, typeSize * oldCount, typeSize * count, alignment, suppress_gc);

#ifdef DEBUG_TRACE_GC
  if (count > oldCount) {
    printf("@%ju %s[%zd] array freed (-%zd bytes)\n",
           (uintmax_t)previous,
           typeName,
           oldCount,
           typeSize * oldCount);
  }
  printf("@%ju %s[%zd] array allocated (%zd bytes) (resized from @%ju %s[%zd] array (%zd bytes))\n",
         (uintmax_t)retval,
         typeName,
         count,
         typeSize * count,
         (uintmax_t)previous,
         typeName,
         oldCount,
         typeSize * oldCount);
#endif

  return retval;
}

extern inline cb_offset_t
logged_free_array(const char *typeName, cb_offset_t previous, size_t typeSize, size_t oldCount, size_t alignment)
{
  cb_offset_t retval = reallocate(previous, typeSize * oldCount, 0, alignment, false);

#ifdef DEBUG_TRACE_GC
  printf("@%ju %s[%zd] array freed (-%zd bytes)\n",
         (uintmax_t)previous,
         typeName,
         oldCount,
         typeSize * oldCount);
#endif

  assert(retval == CB_NULL);
  return retval;
}


bool isWhite(Value value);
cb_offset_t deriveMutableObjectLayer(ObjID id, cb_offset_t object_offset);
void grayObject(const OID<Obj> objectOID);
void grayValue(Value value);
void collectGarbage();
void freeObjects();

#endif
