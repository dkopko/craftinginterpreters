//> Chunks of Bytecode memory-h
#ifndef clox_memory_h
#define clox_memory_h

#include "cb_integration.h"
#include "object.h"

static const size_t alloc_header_size = sizeof(size_t) + sizeof(size_t) + sizeof(bool);

// This is only ever used on non-Obj allocations.
#define ALLOCATE(type, count) \
    logged_allocate(#type, sizeof(type), (count), cb_alignof(type), false)

// FIXME This is only ever used Obj deallocations, and should be renamed to FREE_OBJ and shifted to object.h.
#define FREE(type, offset) \
    logged_free(#type, offset, sizeof(type), cb_alignof(type), true)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(previous, type, oldCount, count) \
    logged_grow_array(&thread_cb, &thread_region, #type, previous, sizeof(type), (oldCount), (count), cb_alignof(type), false, false)

#define GROW_ARRAY_NOGC(previous, type, oldCount, count) \
    logged_grow_array(&thread_cb, &thread_region, #type, previous, sizeof(type), (oldCount), (count), cb_alignof(type), false, true)

#define GROW_ARRAY_NOGC_WITHIN(cb, region, previous, type, oldCount, count) \
    logged_grow_array(cb, region, #type, previous, sizeof(type), (oldCount), (count), cb_alignof(type), false, true)

#define FREE_ARRAY(type, previous, oldCount) \
    logged_free_array(#type, previous, sizeof(type), (oldCount), cb_alignof(type), false)


cb_offset_t reallocate_within(struct cb **cb, struct cb_region *region, cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment, bool isObject, bool suppress_gc);

//Implicitly uses thread_cb and thread_region
cb_offset_t reallocate(cb_offset_t previous, size_t oldSize, size_t newSize, size_t alignment, bool isObject, bool suppress_gc);


extern inline cb_offset_t
logged_allocate(const char *typeName, size_t typeSize, size_t count, size_t alignment, bool isObject)
{
  cb_offset_t retval = reallocate(CB_NULL, 0, typeSize * count, alignment, isObject, false);

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
logged_free(const char *typeName, cb_offset_t previous, size_t typeSize, size_t alignment, bool isObject)
{
  cb_offset_t retval = reallocate(previous, typeSize, 0, alignment, isObject, false);

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
logged_grow_array(struct cb **cb, struct cb_region *region, const char *typeName, cb_offset_t previous, size_t typeSize, size_t oldCount, size_t count, size_t alignment, bool isObject, bool suppress_gc)
{
  cb_offset_t retval = reallocate_within(cb, region, previous, typeSize * oldCount, typeSize * count, alignment, isObject, suppress_gc);

  //FIXME add cb and region details to logging?

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
logged_free_array(const char *typeName, cb_offset_t previous, size_t typeSize, size_t oldCount, size_t alignment, bool isObject)
{
  cb_offset_t retval = reallocate(previous, typeSize * oldCount, 0, alignment, isObject, false);

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


bool alloc_is_object_get(const char *mem);
size_t alloc_size_get(const char *mem);
size_t alloc_alignment_get(const char *mem);

bool isWhite(Value value);
bool objectIsDark(const OID<Obj> objectOID);
cb_offset_t deriveMutableObjectLayer(struct cb **cb, struct cb_region *region, ObjID id, cb_offset_t object_offset);
cb_offset_t cloneObject(struct cb **cb, struct cb_region *region, ObjID id, cb_offset_t object_offset);
void grayObject(const OID<Obj> objectOID);
void grayValue(Value value);
void collectGarbage();

#endif
