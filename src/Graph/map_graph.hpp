/*
 * map_graph.hpp
 *
 *  Created on: Nov 21, 2014
 *      Author: Pietro Incardona
 *
 *
 *  Graph structure that store a CSR graph format
 *
 *  This Graph format is suppose to have a list of vertex that store an index x that indicate where
 *  in the adjacency list we have the list of all the neighborhood vertex, plus an adjacency list
 *  that store all the neighborhood for each vertex:
 *
 *  In reality inside Graph_CSR
 *
 *  VertexList store the Vertex index that indicate the end of the neighborhood list,
 *  the start is indicated by i * v_slot (id of the vertex, v_slot maximum number of
 *  neighborhood for a vertex)
 *
 *  EdgeList store for each vertex at position i * v_slot (id of the vertex, v_slot maximum
 *  number of neighborhood for a vertex) the list of all the neighborhood of the vertex i
 *
 *  Example
 *
 *  Suppose an undirected graph of 4 vertex
 *
 *  1 -> 2 3 4
 *  2 -> 1
 *  3 -> 4 1
 *  4 -> 1 3
 *
 *  suppose that v_slot is 4 ( each vertex has less tha 4 neighborhood  )
 *
 *  we will have
 *
 *  Vertex list 3 5 10 14
 *  Edge list   2 3 4 0 1 0 0 0 4 1 0 0 1 3 0 0
 *
 *  Vertex properties and edge properties are stored in a separate structure
 *
 */

#ifndef MAP_GRAPH_HPP_
#define MAP_GRAPH_HPP_

#include "Vector/map_vector.hpp"
#include <unordered_map>
#ifdef METIS_GP
#include "metis_util.hpp"
#endif

#define NO_EDGE -1

/*! \brief class with no edge
 *
 */
class no_edge {
public:
	typedef boost::fusion::vector<> type;
	typedef typename memory_traits_inte<type>::type memory_int;
	typedef typename memory_traits_lin<type>::type memory_lin;

	type data;

	static const unsigned int max_prop = 0;
};

template<typename V, typename E, template<typename, typename, typename,
		unsigned int> class VertexList, template<typename, typename, typename,
		unsigned int> class EdgeList, typename Memory, typename grow_p>
class Graph_CSR;

/*! \brief Structure used inside GraphCSR an edge
 *
 * For each vertex the first number "vid" store the target node, the second number store
 * edge id that store all the properties
 *
 */

class e_map {
public:
	typedef boost::fusion::vector<size_t, size_t> type;
	typedef typename memory_traits_inte<type>::type memory_int;
	typedef typename memory_traits_lin<type>::type memory_lin;

	type data;

	static const unsigned int vid = 0;
	static const unsigned int eid = 1;
	static const unsigned int max_prop = 2;

	// Setter method

	inline void setvid(size_t vid_) {
		boost::fusion::at_c<0>(data) = vid_;
	}
	;
	inline void seteid(size_t eid_) {
		boost::fusion::at_c<1>(data) = eid_;
	}
	;
};

class edge_key {
public:

	// Actual source node
	size_t pos;

	//Actual target node
	size_t pos_e;

	void begin() {
		pos = 0;
		pos_e = 0;
	}

	/*! \brief Get the source id of this edge
	 *
	 * \return the source of this edge
	 *
	 */
	size_t & source() {
		return pos;
	}

	/*! \brief Return the target of this edge
	 *
	 * \return the target of this edge
	 *
	 */

	size_t & target_t() {
		return pos_e;
	}
};

/*! \brief Graph edge iterator
 *
 */

template<typename Graph>
class edge_iterator {
	//! Graph
	const Graph & g;

	// Actual edge key
	edge_key ek;

public:

	/*! \brief Constructor
	 *
	 * \param g Graph on where iterate
	 *
	 */

	edge_iterator(const Graph & g) :
			g(g) {
		ek.begin();
	}

	/*! \brief Get the next element
	 *
	 * \return the next grid_key
	 *
	 */

	edge_iterator & operator++() {
		// Check if reach the end of the edge list
		if (ek.pos_e + 1 >= g.getNChilds(ek.pos)) {
			// increment the node position and reset
			ek.pos++;
			ek.pos_e = 0;

			// skip every vertex without edges
			while (ek.pos < g.getNVertex() && g.getNChilds(ek.pos) == 0) {
				ek.pos++;
			}
		} else {
			// increment the edge position
			ek.pos_e++;
		}

		return *this;
	}

	/*! \brief Check if there is the next element
	 *
	 * Check if there is the next element
	 *
	 * \return true if there is the next, false otherwise
	 *
	 */

	bool isNext() {
		if (ek.pos < g.getNVertex()) {
			//! we did not reach the end of the node

			return true;
		}

		//! we reach the end of the node
		return false;
	}

	/*! \brief Get the actual key
	 *
	 * Get the actual key
	 *
	 * \return the actual key
	 *
	 */
	edge_key get() {
		return ek;
	}

	/*! \brief Return the source node
	 *
	 * \return the source node
	 *
	 */
	size_t source() {
		return ek.pos;
	}

	/*! \brief Return the target node
	 *
	 * \return the target node
	 *
	 */
	size_t target() {
		return g.getChild(ek.pos, ek.pos_e);
	}
};

/*! \brief Structure that store a graph in CSR format or basically in compressed adjacency matrix format
 *
 * \param V each vertex will encapsulate have this type
 * \param E each edge will encapsulate this type
 * \param device Type of device / basicaly it select the layout
 *        for device_cpu is (x_1, p1_1, p2_1, p3_1 ....), ... ( x_n, p1_1, p2_1, p3_1, ...)
 *        for device_gpu is (x_1, ... , x_n) ... (p1_n, ... pn_n)
 *        where x_1 is the index where it end the list of the neighborhood list and pj_k is
 *        the property j for the vertex j. Basically in the first case one array will store
 *        index and property of each vertex, in the second case several array will store
 *        index and property
 *
 * \param VertexList structure that store the list of Vertex
 * \param EdgeList structure that store the list of edge
 *
 * \warning This graph is suitable only when we know the graph structure and we build
 * the graph adding vertexes and edges, removing vertex and edge is EXTREMLY expensive
 *
 * ### Define vertex and edge of the graph
 * \snippet graph_unit_tests.hpp Define vertex and edge of the graph
 * ### Create a Cartesian graph
 * \snippet graph_unit_tests.hpp Create a Cartesian graph
 * ### Create a tree graph with no edge properties
 * \snippet graph_unit_tests.hpp Create a tree graph with no edge properties
 *
 */

template<typename V, typename E = no_edge, template<typename, typename,
		typename, unsigned int> class VertexList = openfpm::vector,
		template<typename, typename, typename, unsigned int> class EdgeList = openfpm::vector,
		typename Memory = HeapMemory,
		typename grow_p = openfpm::grow_policy_double>
class Graph_CSR {
	//! number of slot per vertex
	size_t v_slot;

	//! Structure that store the vertex properties
	VertexList<V, Memory, grow_p, openfpm::vect_isel<V>::value> v;

	//! Vector that stores 'id's in the position of the vertex in the main vector (needed to fast access by property 'id')
	std::unordered_map<size_t, size_t> map_ids;

	//! Structure that store the number of adjacent vertex in e_l for each vertex
	VertexList<size_t, Memory, grow_p, openfpm::vect_isel<size_t>::value> v_l;

	//! Structure that store the edge properties
	EdgeList<E, Memory, grow_p, openfpm::vect_isel<E>::value> e;

	//! Structure that store for each vertex the adjacent the vertex id and edge id (for property into e)
	EdgeList<e_map, Memory, grow_p, openfpm::vect_isel<e_map>::value> e_l;

	//! invalid edge element, when a function try to create an in valid edge this object is returned
	EdgeList<E, Memory, grow_p, openfpm::vect_isel<E>::value> e_invalid;

	/*! \brief add edge on the graph
	 *
	 * add edge on the graph
	 *
	 * \param v1 start vertex
	 * \param v2 end vertex
	 *
	 */

	template<typename CheckPolicy = NoCheck> inline size_t addEdge_(size_t v1,
			size_t v2) {
		// If v1 and v2 does not satisfy some criteria return
		if (CheckPolicy::valid(v1, v.size()) == false)
			return NO_EDGE;
		if (CheckPolicy::valid(v2, v.size()) == false)
			return NO_EDGE;

		// get the number of adjacent vertex
		size_t id_x_end = v_l.template get<0>(v1);

#ifdef DEBUG
		// Check that v1 and v2 exist

		if (v1 >= v.size() || v2 >= v.size()) {
			//std::cout << "Warning graph: creating an edge between vertex that does not exist" << "\n";
		}

		// Check that the edge does not already exist

		for (size_t s = 0; s < id_x_end; s++) {
			if (e_l.template get<e_map::vid>(v1 * v_slot + s) == v2) {
				std::cerr << "Error graph: the edge already exist\n";
			}
		}
#endif

		// Check if there is space for another edge

		if (id_x_end >= v_slot) {
			// Unfortunately there is not space we need to reallocate memory
			// Reallocate with double slot

			// Create an new Graph

			Graph_CSR<V, E, VertexList, EdgeList> g_new(2 * v_slot, v.size());

			// Copy the graph

			for (size_t i = 0; i < v.size(); i++) {
				// copy the property from the old graph

				g_new.v.set(i, v, 2 * i);
			}

			// swap the new graph with the old one

			swap(g_new);
		}

		// Here we are sure than v and e has enough slots to store a new edge
		// Check that e_l has enough space to store new edge
		// should be always e.size == e_l.size

		if (id_x_end >= e_l.size()) {
			// Resize the basic structure

			e_l.resize(v.size() * v_slot);
		}

		// add in e_l the adjacent vertex for v1 and fill the edge id
		e_l.template get<e_map::vid>(v1 * v_slot + id_x_end) = v2;
		e_l.template get<e_map::eid>(v1 * v_slot + id_x_end) = e.size();

		// add an empty edge
		e.resize(e.size() + 1);

		// Increment the ending point
		++v_l.template get<0>(v1);

		// return the created edge
		return e.size() - 1;
	}

public:

	//! Vertex typedef
	typedef V V_type;

	//! Edge typedef
	typedef E E_type;

	//! Object container for the vertex, for example can be encap<...> (map_grid or openfpm::vector)
	typedef typename VertexList<V, Memory, grow_p, openfpm::vect_isel<V>::value>::container V_container;

	//! Object container for the edge, for example can be encap<...> (map_grid or openfpm::vector)
	typedef typename EdgeList<E, Memory, grow_p, openfpm::vect_isel<E>::value>::container E_container;

	/*! \brief It duplicate the graph
	 *
	 * \return a graph duplicate of the first
	 *
	 */

	Graph_CSR<V, E, VertexList, EdgeList, Memory, grow_p> duplicate() {
		Graph_CSR<V, E, VertexList, EdgeList, Memory, grow_p> dup;

		dup.v_slot = v_slot;

		//! duplicate all the structures

		dup.v.swap(v.duplicate());
		dup.v_l.swap(v_l.duplicate());
		dup.e.swap(e.duplicate());
		dup.e_l.swap(e_l.duplicate());
		dup.e_invalid.swap(e_invalid.duplicate());

		return dup;
	}

	/*! \brief Constructor
	 *
	 * Constructor
	 *
	 */
	Graph_CSR() :
			Graph_CSR(0, 16) {
	}

	/*! \brief Constructor
	 *
	 * Constructor
	 *
	 */
	Graph_CSR(size_t n_vertex) :
			Graph_CSR(n_vertex, 16) {
	}

	/*! \brief Constructor
	 *
	 * Constructor
	 *
	 */
	Graph_CSR(size_t n_vertex, size_t n_slot) :
			v_slot(n_slot) {
		//! Creating n_vertex into the graph
		v.resize(n_vertex);
		//! Creating n_vertex adjacency list counters
		v_l.resize(n_vertex);
		//! no edge set the counter to zero
		v_l.fill(0);
		//! create one invalid edge
		e_invalid.resize(1);
	}

	/*! \brief Copy constructor
	 *
	 */
	Graph_CSR(Graph_CSR<V, E, VertexList, EdgeList, Memory> && g) {
		swap(g);
	}

	/*! \breif Copy the graph
	 * 
	 * \param g graph to copy
	 * 
	 */
	Graph_CSR<V, E, VertexList, EdgeList, Memory> & operator=(Graph_CSR<V, E, VertexList, EdgeList, Memory> && g)
	{
		swap(g);
	}
	
	/*! \breif Copy the graph
	 * 
	 * \param g graph to copy
	 * 
	 */
	Graph_CSR<V, E, VertexList, EdgeList, Memory> & operator=(const Graph_CSR<V, E, VertexList, EdgeList, Memory> & g)
	{
		swap(g.duplicate());
	}
	
	/*! \brief operator to access the vertex
	 *
	 * operator to access the vertex
	 *
	 * \tparam i property to access
	 * \param id of the vertex to access
	 *
	 */
	template<unsigned int i> auto vertex_p(
			size_t id) -> decltype( v.template get<i>(id) ) {
		return v.template get<i>(id);
	}

	/*! \brief Access the vertex
	 *
	 * \tparam i property to access
	 * \param id of the vertex to access
	 *
	 */
	template<unsigned int i> auto vertex_p(
			grid_key_dx<1> id) -> decltype( v.template get<i>(id) ) {
		return v.template get<i>(id);
	}

	/*! \brief Function to access the vertexes
	 *
	 * \param id of the vertex to access
	 *
	 */
	auto vertex(size_t id) -> decltype( v.get(id) ) {
		return v.get(id);
	}

	/*! \brief operator to access the vertex
	 *
	 * operator to access the vertex
	 *
	 * \param id of the vertex to access
	 *
	 */
	auto vertex(grid_key_dx<1> id) -> decltype( v.get(id.get(0)) ) {
		return v.get(id.get(0));
	}

	/*! \brief operator to access the vertex
	 *
	 * operator to access the vertex
	 *
	 * \param id of the vertex to access
	 *
	 */
	auto vertex(openfpm::vector_key_iterator id) -> decltype( v.get(0) ) {
		return v.get(id.get());
	}

	/*! \brief Function to access the vertexes
	 *
	 * \param id of the vertex to access
	 *
	 */
	auto vertex(size_t id) const -> const decltype( v.get(id) ) {
		return v.get(id);
	}

	/*! \brief operator to access the vertex
	 *
	 * operator to access the vertex
	 *
	 * \param id of the vertex to access
	 *
	 */
	auto vertex(grid_key_dx<1> id) const -> const decltype( v.get(id.get(0)) ) {
		return v.get(id.get(0));
	}

	/*! \brief operator to access the vertex
	 *
	 * operator to access the vertex
	 *
	 * \param id of the vertex to access
	 *
	 */
	auto vertex(
			openfpm::vector_key_iterator id) const -> const decltype( v.get(0) ) {
		return v.get(id.get());
	}

	/*! \brief operator to access the vertex by position (id property)
	 *
	 * operator to access the vertex
	 *
	 * \param 'id' of the vertex to access
	 *
	 */
	auto vertexById(size_t id) -> decltype( v.get(map_ids.at(0)) ) {
		return v.get(map_ids.at(id));
	}

	/*! \brief operator to remap vertex to a new position
	 *
	 * \param old position
	 * \param new position
	 *
	 */
	void set_map_ids(size_t n, size_t o) {
        
        try {
            map_ids.at(n) =  o;
        }
        catch (const std::out_of_range& oor) {
            map_ids.insert( { n, o });
        }

	}
    
    /*! \brief operator to get vertex value given key
     *
     * \param n new id
     * \return old id
     *
     */
    size_t getVertexOldId(size_t n) {
        
        return map_ids.at(n);
        
    }

	/*! \brief operator to init ids vector
	 *
	 * operator to init ids vector
	 *
	 */
	void init_map_ids() {
		map_ids.clear();
		for (size_t i = 0; i < v.size(); i++) {
			map_ids.insert( { i, i });
		}
	}
    
    
    /*! \brief operator to reset ids vector
     *
     * operator to reset ids vector
     *
     */
	void reset_map_ids() {
		map_ids.clear();
	}

	/*! \brief get map_ids
	 *
	 * operator to reset ids vector
	 *
	 */
	std::unordered_map<size_t, size_t> get_map_ids() {
		return map_ids;
	}

	/*! \brief get map_ids
	 *
	 * operator to reset ids vector
	 *
	 */
	size_t get_real_id(size_t i) {
		return map_ids.at(i);
	}

	/*! \brief operator to clear the whole graph
	 *
	 * operator to clear all
	 *
	 */
	void clear() {
		v.clear();
		e.clear();
		reset_map_ids();
		v_l.clear();
		e_l.clear();
		e_invalid.clear();
	}

	/*! \brief Access the edge
	 *
	 * \tparam i property to access
	 * \param id of the edge to access
	 *
	 */
	template<unsigned int i> auto edge_p(
			grid_key_dx<1> id) -> decltype ( e.template get<i>(id) ) {
		return e.template get<i>(id);
	}

	/*! \brief Access the edge
	 *
	 * \tparam i property to access
	 * \param id of the edge to access
	 *
	 */
	template<unsigned int i> auto edge_p(
			size_t id) -> decltype ( e.template get<i>(id) ) {
		return e.template get<i>(id);
	}

	/*! \brief Access the edge
	 *
	 * \param id of the edge to access
	 *
	 */
	auto edge(grid_key_dx<1> id) const -> const decltype ( e.get(id.get(0)) ) {
		return e.get(id.get(0));
	}

	/*! \brief operator to access the edge
	 *
	 * \param ek key of the edge
	 *
	 */
	auto edge(edge_key ek) const -> const decltype ( e.get(0) ) {
		return e.get(e_l.template get<e_map::eid>(ek.pos * v_slot + ek.pos_e));
	}

	/*! \brief operator to access the edge
	 *
	 * operator to access the edge
	 *
	 * \param id of the edge to access
	 *
	 */
	auto edge(size_t id) const -> const decltype ( e.get(id) ) {
		return e.get(id);
	}

	/*! \brief Return the number of childs of a vertex
	 *
	 * \param c Child id
	 *
	 * \return the number of childs
	 *
	 */

	inline size_t getNChilds(size_t c) const {
		// Get the number of childs

		return v_l.template get<0>(c);
	}

	/*! \brief Return the number of childs of a vertex
	 *
	 * \param c child id
	 *
	 * \return the number of childs
	 *
	 */
	inline size_t getNChilds(
			typename VertexList<V, Memory, grow_p, openfpm::vect_isel<V>::value>::iterator_key & c) {
		// Get the number of childs

		return v_l.template get<0>(c.get());
	}

	/*! \brief Return the number of childs of a vertex by Id
	 *
	 * \param id vertex id
	 *
	 * \return the number of childs
	 *
	 */

	inline size_t getNChildsByVertexId(size_t id) const {
		//Get vertex position in main VertexList
		size_t v = map_ids.at(id);

		// Get the number of childs

		return v_l.template get<0>(v);
	}

	/*! \brief Get the vertex edge
	 *
	 * \param v vertex
	 * \param v_e edge id
	 *
	 */
	inline auto getChildEdge(size_t v, size_t v_e) -> decltype(e.get(0)) {
#ifdef DEBUG
		if (e >= v_l.template get<0>(v * v_slot)) {
			std::cerr << "Error node " << v << " does not have edge " << v_e
					<< "\n";
		}

		if (e.size() >= e_l.template get<e_map::eid>(v * v_slot + v_e)) {
			std::cerr << "Error edge " << v << " does not have edge " << v_e
					<< "\n";
		}
#endif

		// Get the edge id
		return e.get(e_l.template get<e_map::eid>(v * v_slot + v_e));
	}

	/*! \brief Get the child edge
	 *
	 * \param v node
	 * \param i child at position i
	 *
	 * \return the edge id that connect v with the target at position i
	 *
	 */

	inline size_t getChild(size_t v, size_t i) const {
#ifdef DEBUG
		if (i >= v_l.template get<0>(v)) {
			std::cerr << "Error " << __FILE__ << " line: " << __LINE__
					<< "    vertex " << v << " does not have edge " << i
					<< "\n";
		}

		if (e.size() <= e_l.template get<e_map::eid>(v * v_slot + i)) {
			std::cerr << "Error " << __FILE__ << " " << __LINE__ << " edge "
					<< v << " does not have edge " << i << "\n";
		}
#endif
		// Get the edge id
		return e_l.template get<e_map::vid>(v * v_slot + i);
	}

	/*! \brief Get the child edge
	 *
	 * \param v node
	 * \param i child at position i
	 *
	 * \return the target i connected by an edge node, for the node v
	 *
	 */

	inline size_t getChild(
			typename VertexList<V, Memory, grow_p, openfpm::vect_isel<V>::value>::iterator_key & v,
			size_t i) {
#ifdef DEBUG
		if (i >= v_l.template get<0>(v.get())) {
			std::cerr << "Error " << __FILE__ << " line: " << __LINE__
					<< "    vertex " << v.get() << " does not have edge " << i
					<< "\n";
		}

		if (e.size() <= e_l.template get<e_map::eid>(v.get() * v_slot + i)) {
			std::cerr << "Error " << __FILE__ << " " << __LINE__ << " edge "
					<< v.get() << " does not have edge " << i << "\n";
		}
#endif

		// Get the edge id
		return e_l.template get<e_map::vid>(v.get() * v_slot + i);
	}

	/*! \brief Get the child edge from vertex with property id
	 *
	 * \param id id of the vertex
	 * \param i child at position i
	 *
	 * \return the edge id that connect v with the target with id
	 *
	 */

	inline size_t getChildByVertexId(size_t id, size_t i) const {
		//Get vertex position in main VertexList
		size_t v = map_ids.at(id);

		// Get the edge id
		return e_l.template get<e_map::vid>(v * v_slot + i);
	}

	/*! \brief add vertex
	 *
	 * \param vrt Vertex properties
	 *
	 */

	inline void addVertex(const V & vrt) {

		v.add(vrt);

		// Set position to ids vector

		map_ids.insert( { v.size() - 1, v.size() - 1 });

		// Set the number of adjacent vertex for this vertex to 0

		v_l.add(0ul);

		// Add a slot for the vertex adjacency list

		e_l.resize(e_l.size() + v_slot);
	}

	/*! \brief add an empty vertex
	 *
	 */

	inline void addVertex() {

		v.add();

		// Set position to ids vector

		map_ids.insert( { v.size() - 1, v.size() - 1 });

		// Set the number of adjacent vertex for this vertex to 0

		v_l.add(0ul);

		// Add a slot for the vertex adjacency list

		e_l.resize(e_l.size() + v_slot);
	}

	/*! \brief add edge on the graph
	 *
	 * add an edge on the graph
	 *
	 */

	template<typename CheckPolicy = NoCheck> inline auto addEdge(size_t v1,
			size_t v2, E & ed) -> decltype(e.get(0)) {
		long int id_x_end = addEdge_<CheckPolicy>(v1, v2);

		// If there is not edge return an invalid edge, is a kind of stub object
		if (id_x_end == NO_EDGE)
			return e_invalid.get(0);

		// add in e_l the edge properties
		e.set(id_x_end, ed);

		return e.get(id_x_end);
	}

	/*! \brief add edge on the graph
	 *
	 * add edge on the graph
	 *
	 * \param v1 start vertex
	 * \param v2 end vertex
	 *
	 */

	template<typename CheckPolicy = NoCheck> inline auto addEdge(size_t v1,
			size_t v2) -> decltype(e.get(0)) {
		//! add an edge
		long int id_x_end = addEdge_<CheckPolicy>(v1, v2);
		// If there is not edge return an invalid edge, is a kind of stub object
		if (id_x_end == NO_EDGE)
			return e_invalid.get(0);

		//! return the edge to change the properties
		return e.get(id_x_end);
	}

	/*! \brief swap the memory of g with this graph
	 *
	 * swap the memory of g with this graph, it is basically used
	 * for move semantic
	 *
	 */

	inline void swap(Graph_CSR<V, E, VertexList, EdgeList> & g) {
		// switch the memory

		v.swap(g.v);
		e.swap(g.e);
		v_l.swap(g.v_l);
		e_l.swap(g.e_l);
		e_invalid.swap(g.e_invalid);
	}

	/*! \brief Get the vertex iterator
	 *
	 * Get the vertex iterator
	 *
	 * \return an iterator to iterate through all the vertex
	 *
	 */

	inline auto getVertexIterator() const -> decltype(v.getIterator()) {
		return v.getIterator();
	}

	/*! \brief Get the vertex iterator
	 *
	 * Get the vertex iterator
	 *
	 * \return an iterator to iterate through all the edges
	 *
	 */

	inline edge_iterator<Graph_CSR<V, E, VertexList, EdgeList, Memory>> getEdgeIterator() const {
		return edge_iterator<Graph_CSR<V, E, VertexList, EdgeList, Memory>>(
				*this);
	}

	/*! \brief Return the number of the vertex
	 *
	 * \return the number of vertex
	 *
	 */

	inline size_t getNVertex() const {
		return v.size();
	}

	/*! \brief Return the number of edges
	 *
	 * \return the number of edges
	 *
	 */

	inline size_t getNEdge() const {
		return e.size();
	}
};

/*! \brief Simplified implementation of Graph_CSR
 *
 * Used when Graph_CSR is used as a default template argument to avoid 7 arguments
 *
 * [Example]
 *
 * template<template<typename,typename> class T=Graph_CSR_s>
 * class cool_structure
 * {
 * 		T<Vertex,Edge> graph
 * }
 *
 * only 2 parameter are needed, if you use Graph_CSR you have to define 7 regardless that
 * Graph_CSR has some default template
 *
 * template<template<typename,typename> class T=Graph_CSR>
 * class cool_structure
 * {
 * 		T<Vertex,Edge> graph
 * }
 *
 * THIS DO NOT COMPILE
 *
 */
template<typename V, typename E>
class Graph_CSR_s: public Graph_CSR<V, E> {

};

#endif /* MAP_GRAPH_HPP_ */
