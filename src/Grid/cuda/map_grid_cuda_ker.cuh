/*
 * map_grid_cuda_ker.hpp
 *
 *  Created on: Jun 28, 2018
 *      Author: i-bird
 */

#ifndef MAP_GRID_CUDA_KER_HPP_
#define MAP_GRID_CUDA_KER_HPP_

#include "Grid/grid_base_impl_layout.hpp"

/*! \brief this class is a functor for "for_each" algorithm
 *
 * This class is a functor for "for_each" algorithm. For each
 * element of the boost::vector the operator() is called.
 * Is mainly used to copy one encap into another encap object
 *
 * \tparam encap source
 * \tparam encap dst
 *
 */

template<typename T_type>
struct copy_switch_memory_c_no_cpy
{
	//! encapsulated source object
	const typename memory_traits_inte<T_type>::type & src;
	//! encapsulated destination object
	typename memory_traits_inte<T_type>::type & dst;


	/*! \brief constructor
	 *
	 * \param src source encapsulated object
	 * \param dst source encapsulated object
	 *
	 */
	inline copy_switch_memory_c_no_cpy(const typename memory_traits_inte<T_type>::type & src,
			                   	   	   	     typename memory_traits_inte<T_type>::type & dst)
	:src(src),dst(dst)
	{
	};


	//! It call the copy function for each property
	template<typename T>
	inline void operator()(T& t) const
	{
		boost::fusion::at_c<T::value>(dst).mem = boost::fusion::at_c<T::value>(src).mem;
		// Increment the reference of mem
		boost::fusion::at_c<T::value>(dst).mem->incRef();
		boost::fusion::at_c<T::value>(dst).mem_r.bind_ref(boost::fusion::at_c<T::value>(src).mem_r);
		boost::fusion::at_c<T::value>(dst).switchToDevicePtrNoCopy();
	}
};

template<bool inte_or_lin,typename T>
struct grid_gpu_ker_constructor_impl
{
	template<typename ggk_type> static inline void construct(const ggk_type & cpy,ggk_type & this_)
	{
		copy_switch_memory_c_no_cpy<T> bp_mc(cpy.data_,this_.data_);

		boost::mpl::for_each_ref< boost::mpl::range_c<int,0,T::max_prop> >(bp_mc);
	}
};

template<typename T>
struct grid_gpu_ker_constructor_impl<false,T>
{
	template<typename ggk_type> static inline void construct(const ggk_type & cpy,ggk_type & this_)
	{
		this_.data_.mem = cpy.data_.mem;
		// Increment the reference of mem
		this_.data_.mem->incRef();
		this_.data_.mem_r.bind_ref(cpy.data_.mem_r);
		this_.data_.switchToDevicePtrNoCopy();
	}
};

/*! \brief grid interface available when on gpu
 *
 * \tparam n_buf number of template buffers
 *
 */
template<unsigned int dim, typename T, template <typename> class layout_base>
struct grid_gpu_ker
{
	//! grid information
	grid_sm<dim,void> g1;

	//! type of layout of the structure
	typedef typename layout_base<T>::type layout;

	//! layout data
	layout data_;

	grid_gpu_ker()
	{}

	grid_gpu_ker(const grid_sm<dim,void> & g1)
	:g1(g1)
	{
	}

	grid_gpu_ker(const grid_gpu_ker & cpy)
	:g1(cpy.g1)
	{
		grid_gpu_ker_constructor_impl<is_layout_inte<layout_base<T>>::value,T>::construct(cpy,*this);
	}

	/*! \brief Return the internal grid information
	 *
	 * Return the internal grid information
	 *
	 * \return the internal grid
	 *
	 */
	__device__ __host__ const grid_sm<dim,void> & getGrid() const
	{
		return g1;
	}

	/*! \brief Get the reference of the selected element
	 *
	 * \param v1 grid_key that identify the element in the grid
	 *
	 * \return the reference of the element
	 *
	 */
	template <unsigned int p, typename r_type=decltype(mem_get<p,layout_base<T>,layout,grid_sm<dim,T>,grid_key_dx<dim>>::get(data_,g1,grid_key_dx<dim>()))>
	__device__ __host__ inline r_type get(const grid_key_dx<dim> & v1)
	{
		return mem_get<p,layout_base<T>,decltype(this->data_),decltype(this->g1),decltype(v1)>::get(data_,g1,v1);
	}

	/*! \brief Get the const reference of the selected element
	 *
	 * \param v1 grid_key that identify the element in the grid
	 *
	 * \return the const reference of the element
	 *
	 */
	template <unsigned int p, typename r_type=decltype(mem_get<p,layout_base<T>,layout,grid_sm<dim,T>,grid_key_dx<dim>>::get_c(data_,g1,grid_key_dx<dim>()))>
	__device__ __host__ inline const r_type get(const grid_key_dx<dim> & v1) const
	{
		return mem_get<p,layout_base<T>,decltype(this->data_),decltype(this->g1),decltype(v1)>::get_c(data_,g1,v1);
	}

	/*! \brief Get the reference of the selected element
	 *
	 * \param lin_id linearized element that identify the element in the grid
	 *
	 * \return the reference of the element
	 *
	 */
	template <unsigned int p, typename r_type=decltype(mem_get<p,layout_base<T>,layout,grid_sm<dim,T>,grid_key_dx<dim>>::get_lin(data_,g1,0))>
	__device__ __host__ inline r_type get(const size_t lin_id)
	{
		return mem_get<p,memory_traits_inte<T>,decltype(this->data_),decltype(this->g1),grid_key_dx<dim>>::get_lin(data_,g1,lin_id);
	}

	/*! \brief Get the const reference of the selected element
	 *
	 * \param lin_id linearized element that identify the element in the grid
	 *
	 * \return the const reference of the element
	 *
	 */
	template <unsigned int p, typename r_type=decltype(mem_get<p,layout_base<T>,layout,grid_sm<dim,T>,grid_key_dx<dim>>::get_lin(data_,g1,0))>
	__device__ __host__ inline const r_type get(size_t lin_id) const
	{
		return mem_get<p,layout_base<T>,decltype(this->data_),decltype(this->g1),grid_key_dx<dim>>::get_lin(data_,g1,lin_id);
	}

	/*! \brief Get the of the selected element as a boost::fusion::vector
	 *
	 * Get the selected element as a boost::fusion::vector
	 *
	 * \param v1 grid_key that identify the element in the grid
	 *
	 * \see encap_c
	 *
	 * \return an encap_c that is the representation of the object (careful is not the object)
	 *
	 */
	__device__ inline encapc<dim,T,layout> get_o(const grid_key_dx<dim> & v1)
	{
		return mem_geto<dim,T,layout_base<T>,decltype(this->data_),decltype(this->g1),decltype(v1)>::get(data_,g1,v1);
	}

	/*! \brief Get the of the selected element as a boost::fusion::vector
	 *
	 * Get the selected element as a boost::fusion::vector
	 *
	 * \param v1 grid_key that identify the element in the grid
	 *
	 * \see encap_c
	 *
	 * \return an encap_c that is the representation of the object (careful is not the object)
	 *
	 */
	__device__ inline const encapc<dim,T,layout> get_o(const grid_key_dx<dim> & v1) const
	{
		return mem_geto<dim,T,layout_base<T>,decltype(this->data_),decltype(this->g1),decltype(v1)>::get(const_cast<decltype(this->data_) &>(data_),g1,v1);
	}


	__device__ inline void set(const grid_key_dx<dim> & key1,const grid_gpu_ker<dim,T,layout_base> & g, const grid_key_dx<dim> & key2)
	{
		this->get_o(key1) = g.get_o(key2);
	}

	/*! \brief set an element of the grid
	 *
	 * set an element of the grid
	 *
	 * \param dx is the grid key or the position to set
	 * \param obj value to set
	 *
	 */
	template<typename Memory> __device__ inline void set(grid_key_dx<dim> key1, const encapc<1,T,Memory> & obj)
	{
		this->get_o(key1) = obj;
	}
};


#endif /* MAP_GRID_CUDA_KER_HPP_ */