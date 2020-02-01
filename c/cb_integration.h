#ifndef clox_cb_integration_h
#define clox_cb_integration_h

#include <cb.h>
#include <cb_print.h>
#include <cb_region.h>
#include <cb_term.h>

extern __thread struct cb        *thread_cb;
extern __thread struct cb_region  thread_region;
extern __thread cb_offset_t       thread_cutoff_offset;

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


#endif
