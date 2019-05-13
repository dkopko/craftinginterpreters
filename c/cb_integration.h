#ifndef clox_cb_integration_h
#define clox_cb_integration_h

#include <cb.h>
#include <cb_print.h>
#include <cb_region.h>
#include <cb_term.h>

extern __thread struct cb        *thread_cb;
extern __thread struct cb_region  thread_region;
extern __thread cb_offset_t       thread_cutoff_offset;
extern __thread struct ObjTable   thread_objtable;

#define CB_NULL ((cb_offset_t)0)  //FIXME

template<typename T>
struct CBO
{
  cb_offset_t offset_;

  CBO() : offset_(CB_NULL) { }

  CBO(cb_offset_t offset) : offset_(offset) { }

  CBO(CBO<T> const &rhs) : offset_(rhs.offset_) { }

  bool is_nil() {
    return (offset_ == CB_NULL);
  }

  //Underlying offset
  cb_offset_t o() {
    return offset_;
  }

  //Local dereference
  T* lp() {
    return static_cast<T*>(cb_at(thread_cb, offset_));
  }

  //Remote dereference
  T* rp(struct cb *remote_cb) {
    return static_cast<T*>(cb_at(remote_cb, offset_));
  }
};

typedef struct { unsigned int id; } ObjID;

typedef struct ObjTable {
  cb_offset_t root_a;
  cb_offset_t root_b;
  cb_offset_t root_c;
  ObjID next_obj_id;
} ObjTable;

void objtable_init(ObjTable *obj_table);
ObjID objtable_add(ObjTable *obj_table, cb_offset_t offset);
cb_offset_t objtable_lookup(ObjTable *obj_table, ObjID obj_id);


template<typename T>
struct OID
{
  ObjID objid_;

  OID() : objid_(0) { } //FIXME factor out constant

  OID(ObjID objid) : objid_(objid) { }

  OID(OID<T> const &rhs) : objid_(rhs.objid_) { }

  bool is_nil() {
    return (objid_.id == 0); //FIXME factor out constant
  }

  //Underlying id
  OID id() {
    return objid_;
  }

  //Local dereference
  T* lp() {
    return static_cast<T*>(cb_at(thread_cb, objtable_lookup(&thread_objtable, objid_)));
  }

  //Remote dereference
  //FIXME
  //T* rp(struct cb *remote_cb) {
  //  return static_cast<T*>(cb_at(remote_cb, offset_));
  //}
};

int
clox_value_deep_comparator(const struct cb *cb,
                           const struct cb_term *lhs,
                           const struct cb_term *rhs);

int
clox_value_shallow_comparator(const struct cb *cb,
                              const struct cb_term *lhs,
                              const struct cb_term *rhs);

int
clox_value_render(cb_offset_t           *dest_offset,
                  struct cb            **cb,
                  const struct cb_term  *term,
                  unsigned int           flags);

void
clox_on_cb_resize(const struct cb *old_cb, struct cb *new_cb);

#endif
