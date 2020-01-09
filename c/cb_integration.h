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
extern __thread cb_offset_t       thread_darkset_bst;
extern __thread cb_offset_t       pinned_lower_bound;

#define CB_CACHE_LINE_SIZE 64
#define CB_NULL ((cb_offset_t)0)

struct scoped_pin
{
  const char  *func_;
  int          line_;
  cb_offset_t  prev_pin_offset_;
  cb_offset_t  curr_pin_offset_;

  scoped_pin(const char *func, int line);
  ~scoped_pin();
};

#define PIN_SCOPE scoped_pin _sp(__PRETTY_FUNCTION__, __LINE__)

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

  //Remote dereference
  T* mrp(struct cb *remote_cb) const {
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

int objtable_layer_init(struct cb **cb, struct cb_region *region, cb_offset_t *bst_root);
int methods_layer_init(struct cb **cb, struct cb_region *region, cb_offset_t *bst_root);
int fields_layer_init(struct cb **cb, struct cb_region *region, cb_offset_t *bst_root);

void objtable_init(ObjTable *obj_table);
void objtable_add_at(ObjTable *obj_table, ObjID obj_id, cb_offset_t offset);
ObjID objtable_add(ObjTable *obj_table, cb_offset_t offset);
cb_offset_t objtable_lookup(ObjTable *obj_table, ObjID obj_id);
cb_offset_t objtable_lookup_A(ObjTable *obj_table, ObjID obj_id);
cb_offset_t objtable_lookup_B(ObjTable *obj_table, ObjID obj_id);
cb_offset_t objtable_lookup_C(ObjTable *obj_table, ObjID obj_id);
void objtable_invalidate(ObjTable *obj_table, ObjID obj_id);


#define CB_NULL_OID ((ObjID) { 0 })

cb_offset_t resolveAsMutableLayer(ObjID objid);

template<typename T>
struct OID
{
  ObjID objid_;

  OID() : objid_(CB_NULL_OID) { }

  OID(ObjID objid) : objid_(objid) { }

  OID(OID<T> const &rhs) : objid_(rhs.objid_) { }

  bool is_nil() const {
    return (objid_.id == CB_NULL_OID.id);
  }

  //Underlying id
  ObjID id() const {
    return objid_;
  }

  //Underlying offset
  cb_offset_t co() const {
    return objtable_lookup(&thread_objtable, objid_);
  }

  cb_offset_t co_alt(ObjTable *alt_objtable) const {
    return objtable_lookup(alt_objtable, objid_);
  }

  //Underlying offset, if this ObjID exists in A region mapping. CB_NULL otherwise.
  cb_offset_t co_A() const {
    return objtable_lookup_A(&thread_objtable, objid_);
  }

  //Underlying offset, if this ObjID exists in A region mapping. CB_NULL otherwise.
  cb_offset_t co_B() const {
    return objtable_lookup_B(&thread_objtable, objid_);
  }

  //Underlying offset, if this ObjID exists in A region mapping. CB_NULL otherwise.
  cb_offset_t co_C() const {
    return objtable_lookup_C(&thread_objtable, objid_);
  }

  //Underlying offset
  cb_offset_t mo() const {
    return objtable_lookup(&thread_objtable, objid_);
  }

  //Local dereference
  const T* clip() const {
    return static_cast<const T*>(cb_at(thread_cb, co()));
  }

  //Alternate dereference
  const T* clip_alt(ObjTable *alt_objtable) const {
    return static_cast<const T*>(cb_at(thread_cb, co_alt(alt_objtable)));
  }

  //Local dereference, region A only
  const T* clipA() const {
    cb_offset_t o = co_A();
    if (o == CB_NULL) return NULL;
    return static_cast<const T*>(cb_at(thread_cb, o));
  }

  //Local dereference, region B only
  const T* clipB() const {
    cb_offset_t o = co_B();
    if (o == CB_NULL) return NULL;
    return static_cast<const T*>(cb_at(thread_cb, o));
  }

  //Local dereference, region C only
  const T* clipC() const {
    cb_offset_t o = co_C();
    if (o == CB_NULL) return NULL;
    return static_cast<const T*>(cb_at(thread_cb, o));
  }

  //Local mutable dereference
  T* mlip() {
    return static_cast<T*>(cb_at(thread_cb, resolveAsMutableLayer(objid_)));
  }

  //Remote dereference
  //T* rp(struct cb *remote_cb) {
  //  return static_cast<T*>(cb_at(remote_cb, offset_));
  //}
};


size_t
clox_no_external_size(const struct cb      *cb,
                      const struct cb_term *term);

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
clox_value_no_external_size(const struct cb      *cb,
                            const struct cb_term *term);

void
clox_on_cb_resize(const struct cb *old_cb, struct cb *new_cb);



struct gc_request
{
  struct cb        *orig_cb;

  //Objtable
  struct cb_region  objtable_new_region;
  cb_offset_t       objtable_root_b;
  cb_offset_t       objtable_root_c;

  //Tristack
  struct cb_region  tristack_new_region;
  unsigned int      tristack_abi; // A base index  (mutable region)
  cb_offset_t       tristack_bbo; // B base offset
  unsigned int      tristack_bbi; // B base index
  cb_offset_t       tristack_cbo; // C base offset
  unsigned int      tristack_cbi; // C base index (always 0, really)
  unsigned int      tristack_stackDepth;  // [0, stack_depth-1] are valid entries.

  //Triframes
  struct cb_region  triframes_new_region;
  unsigned int      triframes_abi; // A base index  (mutable region)
  cb_offset_t       triframes_bbo; // B base offset
  unsigned int      triframes_bbi; // B base index
  cb_offset_t       triframes_cbo; // C base offset
  unsigned int      triframes_cbi; // C base index (always 0, really)
  unsigned int      triframes_frameCount;  // [0, stack_depth-1] are valid entries.

  //NOTE: openUpvalues need no special handling, as they are simply a linked
  // list through OIDs in the objtable.  As long as they are grayed, they will
  // be consolidated through objtable consolidation.

  //Strings
  struct cb_region  strings_new_region;
  cb_offset_t       strings_root_b;
  cb_offset_t       strings_root_c;

  //Globals
  struct cb_region  globals_new_region;
  cb_offset_t       globals_root_b;
  cb_offset_t       globals_root_c;
};

struct gc_response
{
  cb_offset_t  objtable_new_root_b;

  cb_offset_t  tristack_new_bbo; // B base offset
  unsigned int tristack_new_bbi; // B base index (always 0, really)

  cb_offset_t  triframes_new_bbo; // B base offset
  unsigned int triframes_new_bbi; // B base index (always 0, really)

  cb_offset_t  strings_new_root_b;

  cb_offset_t  globals_new_root_b;
};

int gc_init(void);
int gc_perform(struct gc_request *req, struct gc_response *resp);


#endif
