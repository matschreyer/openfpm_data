/*
 * util_test.hpp
 *
 *  Created on: May 31, 2015
 *      Author: Pietro Incardona
 */

#ifndef UTIL_TEST_HPP_
#define UTIL_TEST_HPP_

#include "object_util.hpp"
#include "Point_test.hpp"
#include "util/ct_array.hpp"

BOOST_AUTO_TEST_SUITE( util_test )

BOOST_AUTO_TEST_CASE( object_prop_copy )
{
	{
	//! [object copy example]
	typedef Point_test<float> p;
	typedef Point_test<float>::type vboost;
	typedef object_creator<Point_test<float>::type,0,1,4>::type vboost_red;

	object<vboost> src;
	object<vboost_red> dst;

	// fill the source properties x,y,v = 0,1,4 with data

	boost::fusion::at_c<p::x>(src.data) = 1.0;
	boost::fusion::at_c<p::y>(src.data) = 2.0;

	for (size_t i = 0 ; i < 3 ;  i++)
		boost::fusion::at_c<p::v>(src.data)[i] = i + 5.0;

	// copy from src to dst
	object_si_d<object<vboost>,object<vboost_red>,NORMAL,0,1,4>(src,dst);

	// Check the result
	BOOST_REQUIRE_EQUAL(boost::fusion::at_c<0>(dst.data),1.0);
	BOOST_REQUIRE_EQUAL(boost::fusion::at_c<1>(dst.data),2.0);

	for (size_t i = 0 ; i < 3 ;  i++)
		BOOST_REQUIRE_EQUAL(boost::fusion::at_c<2>(dst.data)[i],i + 5.0);

	//! [object copy example]

	//! [object copy encap example]

	typedef encapc<1,Point_test<float>,openfpm::vector<Point_test<float>>::memory_t> encap_src;
	typedef encapc<1,object<vboost_red>,openfpm::vector<object<vboost_red>>::memory_t> encap_dst;

	openfpm::vector<p> v_point;
	openfpm::vector<object<vboost_red>> v_point_red;

	v_point.resize(2);
	v_point_red.resize(2);

	v_point.template get<p::x>(0) = 1.0;
	v_point.template get<p::y>(0) = 2.0;

	for (size_t i = 0 ; i < 3 ;  i++)
		v_point.template get<p::v>(0)[i] = i + 5.0;

	auto src_e = v_point.get(0);
	auto dst_e = v_point_red.get(0);

	object_si_d<encap_src,encap_dst,ENCAP,0,1,4>(src_e,dst_e);

	BOOST_REQUIRE_EQUAL(v_point_red.get(0).template get<0>(),1.0);
	BOOST_REQUIRE_EQUAL(v_point_red.get(0).template get<1>(),2.0);

	for (size_t i = 0 ; i < 3 ;  i++)
		BOOST_REQUIRE_EQUAL(v_point_red.get(0).template get<2>()[i],i + 5.0);

	//! [object copy encap example]
	}

	{
	// Object write test

	//! [object write example]
	typedef Point_test<float> p;
	typedef Point_test<float>::type vboost;
	typedef object_creator<Point_test<float>::type,0,1,4>::type vboost_red;

	object<vboost> dst;
	object<vboost_red> src;

	// fill the source properties 0,1,2 with data

	boost::fusion::at_c<0>(src.data) = 1.0;
	boost::fusion::at_c<1>(src.data) = 2.0;

	for (size_t i = 0 ; i < 3 ;  i++)
		boost::fusion::at_c<2>(src.data)[i] = i + 5.0;

	// copy from src to dst
	object_s_di<object<vboost_red>,object<vboost>,NORMAL,0,1,4>(src,dst);

	// Check the result
	BOOST_REQUIRE_EQUAL(boost::fusion::at_c<p::x>(dst.data),1.0);
	BOOST_REQUIRE_EQUAL(boost::fusion::at_c<p::y>(dst.data),2.0);

	for (size_t i = 0 ; i < 3 ;  i++)
		BOOST_REQUIRE_EQUAL(boost::fusion::at_c<p::v>(dst.data)[i],i + 5.0);

	//! [object write example]

	//! [object write encap example]

	typedef encapc<1,Point_test<float>,openfpm::vector<Point_test<float>>::memory_t> encap_dst;
	typedef encapc<1,object<vboost_red>,openfpm::vector<object<vboost_red>>::memory_t> encap_src;

	openfpm::vector<p> v_point;
	openfpm::vector<object<vboost_red>> v_point_red;

	v_point.resize(2);
	v_point_red.resize(2);

	v_point_red.template get<0>(0) = 11.0;
	v_point_red.template get<1>(0) = 12.0;

	for (size_t i = 0 ; i < 3 ;  i++)
		v_point_red.template get<2>(0)[i] = i + 15.0;

	auto dst_e = v_point.get(0);
	auto src_e = v_point_red.get(0);

	object_s_di<encap_src,encap_dst,ENCAP,0,1,4>(src_e,dst_e);

	BOOST_REQUIRE_EQUAL(v_point.get(0).template get<p::x>(),11.0);
	BOOST_REQUIRE_EQUAL(v_point.get(0).template get<p::y>(),12.0);

	for (size_t i = 0 ; i < 3 ;  i++)
		BOOST_REQUIRE_EQUAL(v_point.get(0).template get<p::v>()[i],i + 15.0);

	//! [object write encap example]
	}
}

//! [Metafunction definition]

template<size_t index, size_t N> struct MetaFunc {
   enum { value = index + N };
};

//! [Metafunction definition]

BOOST_AUTO_TEST_CASE( generate_array )
{
	{
	//! [compile time array]
	const size_t count = 5;
	typedef typename ::generate_array<size_t,count, MetaFunc>::result ct_test;

	// ct_test::data is equivalent to const size_t [5] = {5,6,7,8,9}

	for (size_t i = 0 ; i < count; ++i)
	{
		const size_t ct_val = ct_test::data[i];
		BOOST_REQUIRE_EQUAL(ct_val,count+i);
	}
	//! [compile time array]
	}

	// check constexpr compile time array as template parameters

	{
	//! [constexpr array]
	const size_t count = 5;
	typedef typename ::generate_array_constexpr<size_t,count, MetaFunc>::result ct_test_ce;

	// ct_test_ce::data is equivalent to constexpr size_t [5] = {5,6,7,8,9}

	const size_t ct_calc = MetaFunc<ct_test_ce::data[0],ct_test_ce::data[1]>::value;
	BOOST_REQUIRE_EQUAL(ct_calc,11);
	//! [constexpr array]
	}
}

BOOST_AUTO_TEST_SUITE_END()

#endif /* UTIL_TEST_HPP_ */