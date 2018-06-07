/*
 * boost_multi_array_iterator_openfpm.hpp
 *
 *  Created on: Jun 7, 2018
 *      Author: i-bird
 */

#ifndef OPENFPM_DATA_SRC_UTIL_BOOST_BOOST_MULTI_ARRAY_ITERATOR_OPENFPM_HPP_
#define OPENFPM_DATA_SRC_UTIL_BOOST_BOOST_MULTI_ARRAY_ITERATOR_OPENFPM_HPP_


//
// iterator.hpp - implementation of iterators for the
// multi-dimensional array class
//

#include "boost/iterator/iterator_facade.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include "boost/boost_multi_array_base_openfpm.hpp"

namespace boost {
namespace detail {
namespace multi_array {

template <class T>
struct operator_arrow_proxy_openfpm
{
  operator_arrow_proxy_openfpm(T const& px) : value_(px) {}
  T* operator->() const { return &value_; }
  // This function is needed for MWCW and BCC, which won't call operator->
  // again automatically per 13.3.1.2 para 8
  operator T*() const { return &value_; }
  mutable T value_;
};

/////////////////////////////////////////////////////////////////////////
// iterator components
/////////////////////////////////////////////////////////////////////////

template <typename T, typename TPtr, typename NumDims, typename Reference,
          typename IteratorCategory>
class array_iterator_openfpm;

template <typename T, typename TPtr, typename NumDims, typename Reference,
          typename IteratorCategory>
class array_iterator_openfpm
  : public
    iterator_facade<
        array_iterator_openfpm<T,TPtr,NumDims,Reference,IteratorCategory>
      , typename associated_types_openfpm<T,NumDims>::value_type
      , IteratorCategory
      , Reference
    >
    , private
          value_accessor_generator_openfpm<T,NumDims>::type
{
  friend class iterator_core_access;
  typedef detail::multi_array::associated_types_openfpm<T,NumDims> access_t;

  typedef iterator_facade<
            array_iterator_openfpm<T,TPtr,NumDims,Reference,IteratorCategory>
      , typename detail::multi_array::associated_types_openfpm<T,NumDims>::value_type
      , boost::random_access_traversal_tag
      , Reference
    > facade_type;

  typedef typename access_t::index index;
  typedef typename access_t::size_type size_type;

#ifndef BOOST_NO_MEMBER_TEMPLATE_FRIENDS
  template <typename, typename, typename, typename, typename>
    friend class array_iterator_openfpm;
#else
 public:
#endif

  index idx_;
  TPtr base_;
  const size_type* extents_;
  const index* strides_;
  const index* index_base_;

public:
  // Typedefs to circumvent ambiguities between parent classes
  typedef typename facade_type::reference reference;
  typedef typename facade_type::value_type value_type;
  typedef typename facade_type::difference_type difference_type;

  array_iterator_openfpm() {}

  array_iterator_openfpm(index idx, TPtr base, const size_type* extents,
                const index* strides,
                const index* index_base) :
    idx_(idx), base_(base), extents_(extents),
    strides_(strides), index_base_(index_base) { }

  template <typename OPtr, typename ORef, typename Cat>
  array_iterator_openfpm(
      const array_iterator_openfpm<T,OPtr,NumDims,ORef,Cat>& rhs
    , typename boost::enable_if_convertible<OPtr,TPtr>::type* = 0
  )
    : idx_(rhs.idx_), base_(rhs.base_), extents_(rhs.extents_),
    strides_(rhs.strides_), index_base_(rhs.index_base_) { }


  // RG - we make our own operator->
  operator_arrow_proxy_openfpm<reference>
  operator->() const
  {
    return operator_arrow_proxy_openfpm<reference>(this->dereference());
  }


  reference dereference() const
  {
    typedef typename value_accessor_generator_openfpm<T,NumDims>::type accessor;
    return accessor::access(boost::type<reference>(),
                            idx_,
                            base_,
                            extents_,
                            strides_,
                            index_base_);
  }

  void increment() { ++idx_; }
  void decrement() { --idx_; }

  template <class IteratorAdaptor>
  bool equal(IteratorAdaptor& rhs) const {
    const std::size_t N = NumDims::value;
    return (idx_ == rhs.idx_) &&
      (base_ == rhs.base_) &&
      ( (extents_ == rhs.extents_) ||
        std::equal(extents_,extents_+N,rhs.extents_) ) &&
      ( (strides_ == rhs.strides_) ||
        std::equal(strides_,strides_+N,rhs.strides_) ) &&
      ( (index_base_ == rhs.index_base_) ||
        std::equal(index_base_,index_base_+N,rhs.index_base_) );
  }

  template <class DifferenceType>
  void advance(DifferenceType n) {
    idx_ += n;
  }

  template <class IteratorAdaptor>
  typename facade_type::difference_type
  distance_to(IteratorAdaptor& rhs) const {
    return rhs.idx_ - idx_;
  }


};

} // namespace multi_array
} // namespace detail
} // namespace boost



#endif /* OPENFPM_DATA_SRC_UTIL_BOOST_BOOST_MULTI_ARRAY_ITERATOR_OPENFPM_HPP_ */