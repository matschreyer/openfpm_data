/*
 * GraphMLWriter_unit_tests.hpp
 *
 *  Created on: Dec 9, 2014
 *      Author: i-bird
 */

#ifndef GRAPHMLWRITER_UNIT_TESTS_HPP_
#define GRAPHMLWRITER_UNIT_TESTS_HPP_

#define GS_SIZE 8

#include "GraphMLWriter.hpp"
#include "VTKWriter.hpp"
#include "Graph/CartesianGraphFactory.hpp"
#include "util.hpp"

BOOST_AUTO_TEST_SUITE( graphml_writer_test )

/*!
 *
 * Test node and edge
 *
 */

struct ne_cp
{
	//! The node contain several properties
	typedef boost::fusion::vector<float,float,float,double,long int,int,std::string> type;

	typedef typename memory_traits_inte<type>::type memory_int;
	typedef typename memory_traits_lin<type>::type memory_lin;

	//! The data
	type data;

	//! x property id in boost::fusion::vector
	static const unsigned int x = 0;
	//! y property id in boost::fusion::vector
	static const unsigned int y = 1;
	//! z property id in boost::fusion::vector
	static const unsigned int z = 2;
	//! double_num property id in boost::fusion::vector
	static const unsigned int double_num = 3;
	//! long_num property id in boost::fusion::vector
	static const unsigned int long_num = 4;
	//! integer property id in boost::fusion::vector
	static const unsigned int integer = 5;
	//! string property id in boost::fusion::vector
	static const unsigned int string = 6;
	//! total number of properties boost::fusion::vector
	static const unsigned int max_prop = 7;

	//! define attributes names
	struct attributes
	{
		static const std::string name[max_prop];
	};

	//! type of the spatial information
	typedef float s_type;
};

// Initialize the attributes strings array
const std::string ne_cp::attributes::name[] = {"x","y","z","double_num","long_num","integer","string"};

BOOST_AUTO_TEST_CASE( graphml_writer_use)
{
	Graph_CSR<ne_cp,ne_cp> g_csr2;

	// Add 4 vertex and connect

	struct ne_cp n1;
	g_csr2.addVertex(n1);
	g_csr2.addVertex(n1);
	g_csr2.addVertex(n1);
	g_csr2.addVertex(n1);

	g_csr2.addEdge(0,1);
	g_csr2.addEdge(2,1);
	g_csr2.addEdge(3,1);
	g_csr2.addEdge(2,0);
	g_csr2.addEdge(3,2);

	// Create a graph ML
	GraphMLWriter<Graph_CSR<ne_cp,ne_cp>> gv2(g_csr2);
	gv2.write("test_graph2.graphml");

	// check that match

	bool test = compare("test_graph2.graphml","test_graph2_test.graphml");
	BOOST_REQUIRE_EQUAL(true,test);

	//! Create a graph

	CartesianGraphFactory<3,Graph_CSR<ne_cp,ne_cp>> g_factory;

	// Cartesian grid
	size_t sz[] = {GS_SIZE,GS_SIZE,GS_SIZE};

	// Box
	Box<3,float> box({0.0,0.0,0.0},{1.0,1.0,1.0});

	Graph_CSR<ne_cp,ne_cp> g_csr = g_factory.construct<5,float,2,ne_cp::x,ne_cp::y,ne_cp::z>(sz,box);

	// Create a graph ML
	GraphMLWriter<Graph_CSR<ne_cp,ne_cp>> gw(g_csr);
	gw.write("test_graph.graphml");


	// check that match
	test = compare("test_graph.graphml","test_graph_test.graphml");
	BOOST_REQUIRE_EQUAL(true,test);
}

BOOST_AUTO_TEST_SUITE_END()


#endif /* GRAPHMLWRITER_UNIT_TESTS_HPP_ */
