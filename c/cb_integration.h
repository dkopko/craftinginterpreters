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

  bool is_nil() const {
    return (offset_ == CB_NULL);
  }

  //Underlying offset
  cb_offset_t co() const {
    return offset_;
  }

  //Underlying offset
  cb_offset_t mo() const {
    return offset_;
  }

  //Local dereference
  const T* clp() const {
    return static_cast<const T*>(cb_at(thread_cb, offset_));
  }

  //Local dereference
  T* mlp() {
    return static_cast<T*>(cb_at(thread_cb, offset_));
  }
  //Remote dereference
  const T* crp(struct cb *remote_cb) const {
    return static_cast<T*>(cb_at(remote_cb, offset_));
  }
};

typedef struct { uint64_t id; } ObjID;

typedef struct ObjTable {
  cb_offset_t root_a;
  cb_offset_t root_b;
  cb_offset_t root_c;
  ObjID next_obj_id;
} ObjTable;

void objtable_init(ObjTable *obj_table);
ObjID objtable_add(ObjTable *obj_table, cb_offset_t offset);
cb_offset_t objtable_lookup(ObjTable *obj_table, ObjID obj_id);
void objtable_invalidate(ObjTable *obj_table, ObjID obj_id);


#define CB_NULL_OID ((ObjID) { 0 })  //FIXME

template<typename T>
struct OID
{
  ObjID objid_;

  OID() : objid_(CB_NULL_OID) { } //FIXME factor out constant

  OID(ObjID objid) : objid_(objid) { }

  OID(OID<T> const &rhs) : objid_(rhs.objid_) { }

  bool is_nil() const {
    return (objid_.id == 0); //FIXME factor out constant
  }

  //Underlying id
  ObjID id() const {
    return objid_;
  }

  //Underlying offset
  cb_offset_t co() const {
    return objtable_lookup(&thread_objtable, objid_);
  }

  //Underlying offset
  cb_offset_t mo() const {
    return objtable_lookup(&thread_objtable, objid_);
  }

  //Local dereference
  const T* clip() const {
    return static_cast<const T*>(cb_at(thread_cb, co()));
  }

  //Local dereference
  T* mlip() {
    return static_cast<T*>(cb_at(thread_cb, mo()));
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

size_t
clox_value_external_size(const struct cb      *cb,
                         const struct cb_term *term);

void
clox_on_cb_resize(const struct cb *old_cb, struct cb *new_cb);



struct gc_request
{
  struct cb        *orig_cb;
  struct cb_region  objtable_new_region;
  cb_offset_t       objtable_root_b;
  cb_offset_t       objtable_root_c;
  //FIXME vm.tristack B & C
  //FIXME vm.triframes B & C
  //FIXME openUpvalues??
  //FIXME vm.strings B & C
  //FIXME vm.globals B & C
  //FIXME thread_objtable B & C
  //FIXME grayCompilerRoots() -- entailed by thread_objtable, right??
};

struct gc_response
{
  cb_offset_t objtable_new_root_c;
};

int gc_init(void);
int gc_perform(struct gc_request *req, struct gc_response *resp);


#endif
