#ifndef __NV50_UNORDERED_SET_H__
#define __NV50_UNORDERED_SET_H__

#if (__cplusplus >= 201103L) || defined(ANDROID)
#include <unordered_set>
#else
#include <tr1/unordered_set>
#endif

namespace nv50_ir {

#if __cplusplus >= 201103L
using std::unordered_set;
#elif !defined(ANDROID)
using std::tr1::unordered_set;
#else // Android release before lollipop
using std::isfinite;
typedef std::tr1::unordered_set<void *> voidptr_unordered_set;

template <typename V>
class unordered_set : public voidptr_unordered_set {
  public:
    typedef voidptr_unordered_set _base;
    typedef _base::iterator _biterator;
    typedef _base::const_iterator const_biterator;

    class iterator : public _biterator {
      public:
        iterator(const _biterator & i) : _biterator(i) {}
        V operator*() const { return reinterpret_cast<V>(*_biterator(*this)); }
    };
    class const_iterator : public const_biterator {
      public:
        const_iterator(const iterator & i) : const_biterator(i) {}
        const_iterator(const const_biterator & i) : const_biterator(i) {}
        const V operator*() const { return reinterpret_cast<const V>(*const_biterator(*this)); }
    };

    iterator begin() { return _base::begin(); }
    iterator end() { return _base::end(); }
    const_iterator begin() const { return _base::begin(); }
    const_iterator end() const { return _base::end(); }
};
#endif

} // namespace nv50_ir

#endif // __NV50_UNORDERED_SET_H__
