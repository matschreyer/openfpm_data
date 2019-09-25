//
// Created by tommaso on 6/06/19.
//

#ifndef OPENFPM_PDATA_SPARSEGRIDGPU_HPP
#define OPENFPM_PDATA_SPARSEGRIDGPU_HPP

constexpr int BLOCK_SIZE_STENCIL = 128;

#include <cstdlib>
#include <SparseGridGpu/BlockMapGpu.hpp>
#include <Grid/iterators/grid_skin_iterator.hpp>
#include <SparseGridGpu/Geometry/grid_smb.hpp>
#include "SparseGridGpu_ker.cuh"
#include "SparseGridGpu_kernels.cuh"
#include "Iterators/SparseGridGpu_iterator_sub.hpp"
#include "Geometry/grid_zmb.hpp"
#include "util/stat/common_statistics.hpp"
#include "Iterators/SparseGridGpu_iterator.hpp"

#if defined(OPENFPM_DATA_ENABLE_IO_MODULE) || defined(PERFORMANCE_TEST)
#include "VTKWriter/VTKWriter.hpp"
#endif

// todo: Move all the following utils into some proper file inside TemplateUtils

enum tag_boundaries
{
	NO_CALCULATE_EXISTING_POINTS,
	CALCULATE_EXISTING_POINTS
};

template<unsigned int dim>
struct default_edge
{
	typedef boost::mpl::int_<2> type;
};


template<>
struct default_edge<1>
{
	typedef boost::mpl::int_<64> type;
};

template<>
struct default_edge<2>
{
	typedef boost::mpl::int_<8> type;
};

template<>
struct default_edge<3>
{
	typedef boost::mpl::int_<4> type;
};

template<unsigned int dim, unsigned int blockEdgeSize, typename ... aggr_list>
struct aggregate_transform_datablock_impl
{
	typedef aggregate<DataBlock<aggr_list,IntPow<blockEdgeSize,dim>::value>...> type;
};

template<unsigned int dim, unsigned int blockEdgeSize, typename aggr>
struct aggregate_convert
{
};

template<unsigned int dim, unsigned int blockEdgeSize, typename ... types>
struct aggregate_convert<dim,blockEdgeSize,aggregate<types ...>>
{
	typedef typename aggregate_transform_datablock_impl<dim,blockEdgeSize,types ...>::type type;
};

template<typename aggr>
struct aggregate_add
{
};

template<typename ... types>
struct aggregate_add<aggregate<types ...>>
{
	typedef aggregate<types ..., unsigned char> type;
};

/////////////

enum StencilMode
{
    STENCIL_MODE_INSERT = 0,
    STENCIL_MODE_INPLACE = 1,
    STENCIL_MODE_INSERT_NOFLUSH = 2,
    STENCIL_MODE_INPLACE_NO_SHARED = 3
};

/*! \brief Check if is padding
 *
 *
 */
template<unsigned int dim>
struct NNfull_is_padding_impl
{
	__device__ inline static bool is_padding()
	{
		printf("NNfull_is_padding_impl with dim: %d not implemented yet \n",dim);

		return false;
	}
};

/*! \brief Check if is padding
 *
 *
 */
template<>
struct NNfull_is_padding_impl<3>
{
	template<typename sparseGrid_type, typename coord_type, typename Mask_type,unsigned int eb_size>
	__device__ inline static bool is_padding(sparseGrid_type & sparseGrid, coord_type & coord, Mask_type (& enlargedBlock)[eb_size])
	{
		bool isPadding_ = false;
		for (int i = 0 ; i < 3 ; i++)
		{
			for (int j = 0 ; j < 3 ; j++)
			{
				for (int k = 0 ; k < 3 ; k++)
				{
					grid_key_dx<3,int> key;

					key.set_d(0,i-1);
					key.set_d(1,j-1);
					key.set_d(2,k-1);

					auto nPlusId = sparseGrid.getNeighbourLinIdInEnlargedBlock(coord, key);
					typename std::remove_all_extents<Mask_type>::type neighbourPlus = enlargedBlock[nPlusId];
					isPadding_ = isPadding_ || (!sparseGrid.exist(neighbourPlus));
					if (isPadding_) break;
				}
			}
		}
		return isPadding_;
	}
};


/*! \brief Check if is padding
 *
 *
 */
template<>
struct NNfull_is_padding_impl<2>
{
	template<typename sparseGrid_type, typename coord_type, typename Mask_type,unsigned int eb_size>
	__device__ inline static bool is_padding(sparseGrid_type & sparseGrid, coord_type & coord, Mask_type (& enlargedBlock)[eb_size])
	{
		bool isPadding_ = false;
		for (int i = 0 ; i < 3 ; i++)
		{
			for (int j = 0 ; j < 3 ; j++)
			{
				grid_key_dx<2,int> key;

				key.set_d(0,i-1);
				key.set_d(1,j-1);

				auto nPlusId = sparseGrid.getNeighbourLinIdInEnlargedBlock(coord, key);
				typename std::remove_all_extents<Mask_type>::type neighbourPlus = enlargedBlock[nPlusId];
				isPadding_ = isPadding_ || (!sparseGrid.exist(neighbourPlus));
				if (isPadding_) break;
			}
		}
		return isPadding_;
	}
};

template<unsigned int dim>
struct NNFull
{
	static const int nNN = IntPow<3, dim>::value;

	template<typename indexT, typename blockCoord_type, typename blockMap_type, typename SparseGrid_type>
	__device__ static inline indexT getNNpos(blockCoord_type & blockCoord,
								  blockMap_type & blockMap,
								  SparseGrid_type & sparseGrid,
								  const unsigned int offset)
	{
        // point to the background element
        int neighbourPos = blockMap.size();
        if (offset < nNN && offset != nNN / 2)
        {
        	int cnt = offset;
        	for (int i = 0 ; i < dim ; i++)
        	{
        		int dPos = cnt % 3;
        		cnt /= 3;
        		blockCoord.set_d(i, blockCoord.get(i) + dPos - 1);
        	}

            neighbourPos = blockMap.get_sparse(sparseGrid.getBlockLinId(blockCoord)).id;
        }
        return neighbourPos;
	}

	template<typename indexT, unsigned int blockEdgeSize, typename coordType>
	__host__ static inline indexT getNNskin(coordType & coord, int stencilSupportRadius)
	{
		// linearize the coord

		indexT neighbourNum = 0;

		indexT accu = 1;
		for(int i = 0 ; i < dim ; i++)
		{
			int c = static_cast<int>(coord.get(i)) - static_cast<int>(stencilSupportRadius);
			if (c < 0)
			{
				neighbourNum += 0;
			}
            else if (c >= blockEdgeSize)
            {
                neighbourNum += 2*accu;
            }
            else
            {
            	neighbourNum += accu;
            }
			accu *= 3;
		}

        return neighbourNum;
	}


	template<typename sparseGrid_type, typename coord_type, typename Mask_type,unsigned int eb_size>
	__device__ static inline bool isPadding(sparseGrid_type & sparseGrid, coord_type & coord, Mask_type (& enlargedBlock)[eb_size])
	{
		return NNfull_is_padding_impl<3>::template is_padding(sparseGrid,coord,enlargedBlock);
	}

	/*! \brief given a coordinate writtel in local coordinate for a given it return the neighborhood chunk position and the offset
	 *        in the neighborhood chunk
	 *
	 * \param coord local coordinated
	 * \param NN_index index of the neighborhood chunk
	 * \param offset_nn offset in local coordinates
	 *
	 * \return true if it is inside false otherwise
	 *
	 */
	template<unsigned int blockEdgeSize, typename indexT2>
	__device__ static inline bool getNNindex_offset(grid_key_dx<dim,indexT2> & coord, unsigned int & NN_index, unsigned int & offset_nn)
	{
		bool out = false;
		NN_index = 0;
		offset_nn = 0;

		int cnt = 1;
		int cnt_off = 1;
		for (unsigned int i = 0 ; i < dim ; i++)
		{
			int p = 1 - ((int)(coord.get(i) < 0)) + ((int)(coord.get(i) >= (int)blockEdgeSize));

			NN_index += p*cnt;

			offset_nn += (coord.get(i) + (1 - p)*(int)blockEdgeSize)*cnt_off;

			cnt *= 3;
			cnt_off *= blockEdgeSize;

			out |= (p != 1);
		}

		return out;
	}
};

template<unsigned int dim>
struct NNStar
{
	static const int nNN = IntPow<2, dim>::value;

	template<typename indexT, typename blockCoord_type, typename blockMap_type, typename SparseGrid_type>
	__device__ static inline indexT getNNpos(blockCoord_type & blockCoord,
								  blockMap_type & blockMap,
								  SparseGrid_type & sparseGrid,
								  const unsigned int offset)
	{
        //todo: also do the full neighbourhood version, this is just cross
        int neighbourPos = -1;
        if (offset < 2*dim)
        {
            unsigned int d = offset/2;
            int dPos = blockCoord.get(d) + (offset%2)*2 - 1;
            blockCoord.set_d(d, dPos);
            neighbourPos = blockMap.get_sparse(sparseGrid.getBlockLinId(blockCoord)).id;
        }
        return neighbourPos;
	}

	template<typename indexT, unsigned int blockEdgeSize, typename coordType>
	__host__ static inline indexT getNNskin(coordType & coord, int stencilSupportRadius)
	{
        int neighbourNum = -1;
        int ctr = 0;
        for (int j = 0; j < dim; ++j)
        {
            int c = static_cast<int>(coord.get(j)) - static_cast<int>(stencilSupportRadius);
            if (c < 0)
            {
                neighbourNum = 2*j;
                ++ctr;
            }
            else if (c >= blockEdgeSize)
            {
                neighbourNum = 2*j + 1;
                ++ctr;
            }
        }
        if (ctr > 1) // If we are on a "corner"
        {
            neighbourNum = 0;
        }

        return neighbourNum;
	}

	template<typename sparseGrid_type, typename coord_type, typename Mask_type,unsigned int eb_size>
	__device__ static inline bool isPadding(sparseGrid_type & sparseGrid, coord_type & coord, Mask_type (& enlargedBlock)[eb_size])
	{
		bool isPadding_ = false;
		for (int d=0; d<dim; ++d)
		{
			auto nPlusId = sparseGrid.getNeighbourLinIdInEnlargedBlock(coord, d, 1);
			auto nMinusId = sparseGrid.getNeighbourLinIdInEnlargedBlock(coord, d, -1);
			typename std::remove_all_extents<Mask_type>::type neighbourPlus = enlargedBlock[nPlusId];
			typename std::remove_all_extents<Mask_type>::type neighbourMinus = enlargedBlock[nMinusId];
			isPadding_ = isPadding_ || (!sparseGrid.exist(neighbourPlus));
			isPadding_ = isPadding_ || (!sparseGrid.exist(neighbourMinus));
			if (isPadding_) break;
		}

		return isPadding_;
	}

	/*! \brief given a coordinate give the neighborhood chunk position and the offset in the neighborhood chunk
	 *
	 *
	 */
	__device__ static inline bool getNNindex_offset()
	{
		return false;
	}
};

template<unsigned int nNN_, unsigned int nLoop_>
struct ct_par
{
	static const unsigned int nNN = nNN_;
	static const unsigned int nLoop = nLoop_;
};


template<typename Tsrc,typename Tdst>
class copy_prop_to_vector_block
{
	//! source
	Tsrc src;

	//! destination
	Tdst dst;

	size_t pos;

	unsigned int bPos;

public:

	copy_prop_to_vector_block(Tsrc src, Tdst dst,size_t pos, size_t bPos)
	:src(src),dst(dst),pos(pos),bPos(bPos)
	{}

	//! It call the copy function for each property
	template<typename T>
	inline void operator()(T& t) const
	{
		typedef typename std::remove_reference<decltype(dst.template get<T::value>())>::type copy_rtype;

		meta_copy<copy_rtype>::meta_copy_(src.template get<T::value>()[bPos],dst.template get<T::value>());
	}

};

template<unsigned int dim,
		 typename AggregateT,
		 unsigned int blockEdgeSize = default_edge<dim>::type::value,
		 unsigned int threadBlockSize = 128,
		 typename indexT=long int,
		 template<typename> class layout_base=memory_traits_inte,
		 typename linearizer = grid_smb<dim, blockEdgeSize>>
class SparseGridGpu : public BlockMapGpu<
        typename aggregate_convert<dim,blockEdgeSize,AggregateT>::type,
        threadBlockSize, indexT, layout_base>
{
private:

    const static unsigned char PADDING_BIT = 1;
	typedef typename aggregate_convert<dim,blockEdgeSize,AggregateT>::type AggregateBlockT;
    linearizer gridGeometry;
    grid_sm<dim, int> extendedBlockGeometry;
    grid_sm<dim, int> gridSize;
    unsigned int stencilSupportRadius;
    unsigned int ghostLayerSize;

    //! set of existing points
    //! the formats is id/blockSize = data block poosition id % blockSize = offset
    openfpm::vector_gpu<aggregate<indexT>> e_points;

    //! For stencil in a block-wise computation we have to load blocks + ghosts area. The ghost area live in neighborhood blocks
    //! For example the left ghost margin live in the right part of the left located neighborhood block, the right margin live in the
    //! left part of the of the right located neighborhood block, the top ...
    //! The first index indicate the index of the point in the block + ghost area, the second index indicate the correspondent neighborhood
    //! index (in a star like 0 mean negative x 1 positive x, 1 mean negative y and so on)
    openfpm::vector_gpu<aggregate<short int,short int>> ghostLayerToThreadsMapping;

    openfpm::vector_gpu<aggregate<indexT>> nn_blocks;

    //! temporal
    mutable openfpm::vector_gpu<aggregate<indexT>> tmp;

    typedef SparseGridGpu<dim,AggregateT,blockEdgeSize,threadBlockSize,indexT,layout_base,linearizer> self;

    //! the set of all sub-set to pack
    mutable openfpm::vector_gpu<Box<dim,int>> pack_subs;

protected:
    static constexpr unsigned int blockSize = BlockTypeOf<AggregateBlockT, 0>::size;
    typedef AggregateBlockT AggregateInternalT;

public:

	//! it define that this data-structure is a grid
	typedef int yes_i_am_grid;

    static constexpr unsigned int dims = dim;
    static constexpr unsigned int blockEdgeSize_ = blockEdgeSize;

    typedef linearizer grid_info;

    template<typename Tfunc> using layout_mfunc = memory_traits_inte<Tfunc>;

    typedef sparse_grid_gpu_index<self> base_key;

    /*! \brief return the size of the grid
     *
     * \return Return the size of the grid
     *
     */
    inline size_t size() const
    {
        return this->countExistingElements();
    }

    /*! \brief This is a meta-function return which type of sub iterator a grid produce
     *
     * \return the type of the sub-grid iterator
     *
     */
    template <typename stencil = no_stencil>
    static SparseGridGpu_iterator_sub<dim,self> type_of_subiterator()
    {
        return SparseGridGpu_iterator_sub<dim,self>();
    }

    /*! \brief This is a meta-function return which type of iterator a grid produce
     *
     * \return the type of the sub-grid iterator
     *
     */
    static SparseGridGpu_iterator<dim,self> type_of_iterator()
    {
        return SparseGridGpu_iterator<dim,self>(std::declval<self>());
    }

    template<typename dim3T>
    inline static int dim3SizeToInt(dim3T d)
    {
        return d.x * d.y * d.z;
    }

    inline static int dim3SizeToInt(size_t d)
    {
        return d;
    }

    inline static int dim3SizeToInt(unsigned int d)
    {
        return d;
    }

    template<typename ... v_reduce>
    void flush(mgpu::ofp_context_t &context, flush_type opt = FLUSH_ON_HOST)
    {
        BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>
                ::template flush<v_reduce ...>(context, opt);
    }

private:

    void computeSizeOfGhostLayer()
    {
        unsigned int term1 = 1;
        for (int i = 0; i < dim; ++i)
        {
            term1 *= blockEdgeSize + 2 * stencilSupportRadius;
        }
        unsigned int term2 = 1;
        for (int i = 0; i < dim; ++i)
        {
            term2 *= blockEdgeSize;
        }
        ghostLayerSize = term1 - term2;
    }

    void allocateGhostLayerMapping()
    {
    	ghostLayerToThreadsMapping.resize(ghostLayerSize);
    }

    template<typename stencil_type>
    void computeGhostLayerMapping()
    {
        size_t dimensions[dim],
                origin[dim],
                innerDomainBegin[dim], innerDomainEnd[dim],
                outerBoxBegin[dim], outerBoxEnd[dim],
                bc[dim];
        for (int i = 0; i < dim; ++i)
        {
            dimensions[i] = blockEdgeSize + 2 * stencilSupportRadius;
            origin[i] = 0;
            innerDomainBegin[i] = stencilSupportRadius - 1;
            innerDomainEnd[i] = dimensions[i] - stencilSupportRadius;
            outerBoxBegin[i] = origin[i];
            outerBoxEnd[i] = dimensions[i];
            bc[i] = NON_PERIODIC;
        }
        grid_sm<dim, void> enlargedGrid;
        enlargedGrid.setDimensions(dimensions);
        Box<dim, size_t> outerBox(outerBoxBegin, outerBoxEnd);
        Box<dim, size_t> innerBox(innerDomainBegin, innerDomainEnd);

        grid_skin_iterator_bc<dim> gsi(enlargedGrid, innerBox, outerBox, bc);

        unsigned int i = 0;
        while (gsi.isNext())
        {
            auto coord = gsi.get();
            assert(i < ghostLayerSize);
            mem_id linId = enlargedGrid.LinId(coord);
            // Mapping
            ghostLayerToThreadsMapping.template get<gt>(i) = linId;
            // Now compute the neighbour position to use
            ghostLayerToThreadsMapping.template get<nt>(i) = stencil_type::template getNNskin<indexT,blockEdgeSize>(coord,stencilSupportRadius);
            //
            ++i;
            ++gsi;
        }
        assert(i == ghostLayerSize);

        ghostLayerToThreadsMapping.template hostToDevice<gt,nt>();
    }

    void initialize(const size_t (& res)[dim])
    {
    	gridGeometry = linearizer(res);

        computeSizeOfGhostLayer();
        allocateGhostLayerMapping();
        computeGhostLayerMapping<NNStar<dim>>();

        size_t extBlockDims[dim];
        for (int d=0; d<dim; ++d)
        {
            extBlockDims[d] = blockEdgeSize + 2*stencilSupportRadius;
        }
        extendedBlockGeometry.setDimensions(extBlockDims);
        gridSize.setDimensions(res);
    }

    // This is for testing stencil application in Host, remember to deviceToHost the sparse grid before doing this!
    template <typename stencil, typename... Args>
    void applyStencilInPlaceHost(Args... args)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        const unsigned int dataChunkSize = BlockTypeOf<AggregateBlockT, 0>::size;
        unsigned int numScalars = indexBuffer.size() * dataChunkSize;

        if (numScalars == 0) return;

        SparseGridGpuKernels::applyStencilInPlaceHost
                <dim,
                        BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                        stencil>(
                         indexBuffer,
                         dataBuffer,
                         *this,
                         args...);
    }

    template <typename stencil, typename... Args>
    void applyStencilInPlace(StencilMode & mode,Args... args)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        const unsigned int dataChunkSize = BlockTypeOf<AggregateBlockT, 0>::size;
        unsigned int numScalars = indexBuffer.size() * dataChunkSize;

        if (numScalars == 0) return;

        // NOTE: Here we want to work only on one data chunk per block!
        constexpr unsigned int chunksPerBlock = 1;
        const unsigned int localThreadBlockSize = dataChunkSize * chunksPerBlock;
        const unsigned int threadGridSize = numScalars % localThreadBlockSize == 0
                                            ? numScalars / localThreadBlockSize
                                            : 1 + numScalars / localThreadBlockSize;

        constexpr unsigned int nLoop = UIntDivCeil<(IntPow<blockEdgeSize + 2, dim>::value - IntPow<blockEdgeSize, dim>::value), (blockSize * chunksPerBlock)>::value; // todo: This works only for stencilSupportSize==1

        CUDA_LAUNCH_DIM3((SparseGridGpuKernels::applyStencilInPlace
                <dim,
                BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                stencil>),
                threadGridSize, localThreadBlockSize,
                        indexBuffer.toKernel(),
                        dataBuffer.toKernel(),
                        this->template toKernelNN<stencil::stencil_type::nNN, nLoop>(),
                        args...);
    }

    template <typename stencil, typename... Args>
    void applyStencilInPlaceNoShared(StencilMode & mode,Args... args)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        const unsigned int dataChunkSize = BlockTypeOf<AggregateBlockT, 0>::size;
        unsigned int numScalars = indexBuffer.size() * dataChunkSize;

        if (numScalars == 0) return;

        // NOTE: Here we want to work only on one data chunk per block!
        constexpr unsigned int chunksPerBlock = 1;
        auto ite = e_points.getGPUIterator(BLOCK_SIZE_STENCIL);

        CUDA_LAUNCH((SparseGridGpuKernels::applyStencilInPlaceNoShared
                <dim,
                BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                stencil>),
                ite,
                        indexBuffer.toKernel(),
                        dataBuffer.toKernel(),
                        this->template toKernelNN<stencil::stencil_type::nNN, 0>(),
                        args...);
    }

    //todo: the applyInsert should also allocate the gpu insert buffer, initialize it, etc
    template <typename stencil, typename... Args>
    void applyStencilInsert(StencilMode & mode, Args... args)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        const unsigned int dataChunkSize = BlockTypeOf<AggregateBlockT, 0>::size;
        unsigned int numScalars = indexBuffer.size() * dataChunkSize;

        if (numScalars == 0) return;

        // NOTE: Here we want to work only on one data chunk per block!
        constexpr unsigned int chunksPerBlock = 1;
        const unsigned int localThreadBlockSize = dataChunkSize * chunksPerBlock;
        const unsigned int threadGridSize = numScalars % localThreadBlockSize == 0
                                      ? numScalars / localThreadBlockSize
                                      : 1 + numScalars / localThreadBlockSize;

        constexpr unsigned int nLoop = UIntDivCeil<(IntPow<blockEdgeSize + 2, dim>::value - IntPow<blockEdgeSize, dim>::value), (blockSize * chunksPerBlock)>::value; // todo: This works only for stencilSupportSize==1

        setGPUInsertBuffer(threadGridSize, chunksPerBlock);
//        setGPUInsertBuffer(threadGridSize, localThreadBlockSize);

        CUDA_LAUNCH_DIM3((SparseGridGpuKernels::applyStencilInsert
                <dim,
                BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                stencil>),
                threadGridSize, localThreadBlockSize,
                        indexBuffer.toKernel(),
                        dataBuffer.toKernel(),
                        this->template toKernelNN<stencil::stencil_type::nNN, nLoop>(),
                        args...);


        if (mode != STENCIL_MODE_INSERT_NOFLUSH)
        {
        	mgpu::ofp_context_t ctx;
        	stencil::flush(*this, ctx);
        }
    }

    template <typename stencil, typename... Args>
    void applyBoundaryStencilInPlace(Args... args)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        const unsigned int dataChunkSize = BlockTypeOf<AggregateBlockT, 0>::size;
        unsigned int numScalars = indexBuffer.size() * dataChunkSize;

        if (numScalars == 0) return;

        // NOTE: Here we want to work only on one data chunk per block!
        constexpr unsigned int chunksPerBlock = 1;
        const unsigned int localThreadBlockSize = dataChunkSize * chunksPerBlock;
        const unsigned int threadGridSize = numScalars % localThreadBlockSize == 0
                                            ? numScalars / localThreadBlockSize
                                            : 1 + numScalars / localThreadBlockSize;

        constexpr unsigned int nLoop = UIntDivCeil<(IntPow<blockEdgeSize + 2, dim>::value - IntPow<blockEdgeSize, dim>::value), (blockSize * chunksPerBlock)>::value; // todo: This works only for stencilSupportSize==1

        CUDA_LAUNCH_DIM3((SparseGridGpuKernels::applyBoundaryStencilInPlace
                <dim,
                        BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                        stencil>),
                         threadGridSize, localThreadBlockSize,
                         indexBuffer.toKernel(),
                         dataBuffer.toKernel(),
                         this->template toKernelNN<stencil::stencil_type::nNN, nLoop>(),
                         args...);
    }

public:

    typedef AggregateT value_type;

    SparseGridGpu()
	:stencilSupportRadius(1)
    {};

    /*! \brief resize the SparseGrid
     *
     * \param res indicate the resolution in each dimension
     *
     */
    void resize(size_t (& res)[dim])
    {
    	initialize(res);
    }

    /*! \brief Constructor from glock geometry
     *
     *
     */
    SparseGridGpu(const size_t (& res)[dim], unsigned int stencilSupportRadius = 1)
    :stencilSupportRadius(stencilSupportRadius)
    {
    	initialize(res);
    };

    /*! \brief Constructor from glock geometry
     *
     *
     */
    SparseGridGpu(linearizer & gridGeometry, unsigned int stencilSupportRadius = 1)
            : gridGeometry(gridGeometry),
              stencilSupportRadius(stencilSupportRadius)
    {
    	initialize(gridGeometry.getSize());
    };

    SparseGridGpu_ker
            <
                    dim,
                    blockEdgeSize,
                    typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT,
                    ct_par<0,1>,
                    indexT,
                    layout_base,
                    decltype(extendedBlockGeometry),
                    linearizer
            > toKernel()
    {
        SparseGridGpu_ker
                <
                        dim,
                        blockEdgeSize,
                        typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT,
                        ct_par<0,1>,
                        indexT,
                        layout_base,
                        decltype(extendedBlockGeometry),
                        linearizer
                > toKer(
                BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.toKernel(),
                gridGeometry,
                extendedBlockGeometry,
                stencilSupportRadius,
                ghostLayerToThreadsMapping.toKernel(),
                nn_blocks.toKernel(),
                e_points.toKernel(),
                ghostLayerSize);
        return toKer;
    }

    template<unsigned int nNN, unsigned int nLoop>
    SparseGridGpu_ker
            <
                    dim,
                    blockEdgeSize,
                    typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT,
                    ct_par<nNN,nLoop>,
                    indexT,
                    layout_base,
                    decltype(extendedBlockGeometry),
                    linearizer
            > toKernelNN()
    {
        SparseGridGpu_ker
                <
                        dim,
                        blockEdgeSize,
                        typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT,
                        ct_par<nNN,nLoop>,
                        indexT,
                        layout_base,
                        decltype(extendedBlockGeometry),
                        linearizer
                > toKer(
                BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.toKernel(),
                gridGeometry,
                extendedBlockGeometry,
                stencilSupportRadius,
                ghostLayerToThreadsMapping.toKernel(),
                nn_blocks.toKernel(),
                e_points.toKernel(),
                ghostLayerSize);
        return toKer;
    }

    /*! \brief Return the grid information object
     *
     * \return grid information object
     *
     */
    linearizer & getGrid()
    {
    	return gridGeometry;
    }

    /*! \brief Set the neighborhood type
     *
     * \tparam stencil_type Type of stencil
     *
     */
    template<typename stencil_type>
    void setNNType()
    {
    	computeGhostLayerMapping<stencil_type>();
    }


    constexpr static unsigned int getBlockEdgeSize()
    {
        return blockEdgeSize;
    }

    constexpr unsigned int getBlockSize() const
    {
        return blockSize;
    }

    // Geometry
    template<typename CoordT>
    inline size_t getLinId(CoordT &coord)
    {
        return gridGeometry.LinId(coord);
    }

    inline grid_key_dx<dim, int> getCoord(size_t linId) const
    {
        return gridGeometry.InvLinId(linId);
    }

    inline ite_gpu<dim> getGridGPUIterator(const grid_key_dx<dim, int> & start, const grid_key_dx<dim, int> & stop, size_t n_thr = threadBlockSize)
    {
    	return gridSize.getGPUIterator(start,stop,n_thr);
    }


    /*! \brief Get an element using the point coordinates
     *
     * \tparam p property index
     *
     * \param coord point coordinates
     *
     * \return the element
     *
     */
    template<unsigned int p, typename CoordT>
    auto get(const grid_key_dx<dim,CoordT> & coord) const -> const ScalarTypeOf<AggregateBlockT, p> &
    {
        return BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::template get<p>(gridGeometry.LinId(coord));
    }

    /*! \brief Get an element using sparse_grid_gpu_index (using this index it guarantee that the point exist)
     *
     * \tparam p property index
     *
     * \param element
     *
     * \return the element
     *
     */
    template<unsigned int p>
    auto get(const sparse_grid_gpu_index<self> & coord) const -> const ScalarTypeOf<AggregateBlockT, p> &
    {
        return private_get_data_array().template get<p>(coord.get_cnk_pos_id())[coord.get_data_id()];
    }

    /*! \brief Get an element using sparse_grid_gpu_index (using this index it guarantee that the point exist)
     *
     * \tparam p property index
     *
     * \param element
     *
     * \return the element
     *
     */
    template<unsigned int p>
    auto get(const sparse_grid_gpu_index<self> & coord) -> ScalarTypeOf<AggregateBlockT, p> &
    {
        return private_get_data_array().template get<p>(coord.get_cnk_pos_id())[coord.get_data_id()];
    }

    template<unsigned int p, typename CoordT>
    auto insert(const CoordT &coord) -> ScalarTypeOf<AggregateBlockT, p> &
    {
        return BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::template insert<p>(gridGeometry.LinId(coord));
    }

    /*! \Brief Before inser any element you have to call this function to initialize the insert buffer
     *
     * \param nBlock number of blocks the insert buffer has
     * \param nSlot maximum number of insertion each thread block does
     *
     */
    template<typename dim3T>
    void setGPUInsertBuffer(dim3T nBlock, dim3T nSlot)
    {
        BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>
        ::setGPUInsertBuffer(
                dim3SizeToInt(nBlock),
                dim3SizeToInt(nSlot)
        );
    }

    template<typename stencil_type = NNStar<dim>>
    void tagBoundaries(mgpu::ofp_context_t &context, tag_boundaries opt = tag_boundaries::NO_CALCULATE_EXISTING_POINTS)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        const unsigned int dataChunkSize = BlockTypeOf<AggregateBlockT, 0>::size;
        unsigned int numScalars = indexBuffer.size() * dataChunkSize;

        if (numScalars == 0) return;

        // NOTE: Here we want to work only on one data chunk per block!

        unsigned int localThreadBlockSize = dataChunkSize;
        unsigned int threadGridSize = numScalars % dataChunkSize == 0
                                    ? numScalars / dataChunkSize
                                    : 1 + numScalars / dataChunkSize;

        constexpr unsigned int nLoop = UIntDivCeil<(IntPow<blockEdgeSize + 2, dim>::value - IntPow<blockEdgeSize, dim>::value), (blockSize * 1)>::value; // todo: This works only for stencilSupportSize==1
//        constexpr unsigned int nLoop = IntPow<blockEdgeSize + 2, dim>::value; // todo: This works only for stencilSupportSize==1

        if (stencilSupportRadius == 1)
        {
            CUDA_LAUNCH_DIM3((SparseGridGpuKernels::tagBoundaries<
                    dim,
                    1,
                    BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                    stencil_type>),
                    threadGridSize, localThreadBlockSize,indexBuffer.toKernel(), dataBuffer.toKernel(), this->template toKernelNN<stencil_type::nNN, nLoop>(), nn_blocks.toKernel());
        }
        else if (stencilSupportRadius == 2)
        {
        	CUDA_LAUNCH_DIM3((SparseGridGpuKernels::tagBoundaries<
                    dim,
                    2,
                    BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                    stencil_type>),
                    threadGridSize, localThreadBlockSize,indexBuffer.toKernel(), dataBuffer.toKernel(), this->template toKernelNN<stencil_type::nNN, nLoop>(), nn_blocks.toKernel());
        }
        else if (stencilSupportRadius == 0)
        {
        	CUDA_LAUNCH_DIM3((SparseGridGpuKernels::tagBoundaries<
                    dim,
                    0,
                    BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask,
                    stencil_type>),
                    threadGridSize, localThreadBlockSize,indexBuffer.toKernel(), dataBuffer.toKernel(), this->template toKernelNN<stencil_type::nNN, nLoop>(), nn_blocks.toKernel());
        }
        else
        {
            //todo: KABOOOOOOM!!!
            std::cout << __FILE__ << ":" << __LINE__ << " error: stencilSupportRadius supported only up to 2, passed: " << stencilSupportRadius << std::endl;

        }

        if (opt == tag_boundaries::CALCULATE_EXISTING_POINTS)
        {
        	// first we calculate the existing points
        	openfpm::vector_gpu<aggregate<indexT>> block_points;

        	block_points.resize(indexBuffer.size() + 1);
        	block_points.template get<0>(block_points.size()-1) = 0;
        	block_points.template hostToDevice<0>(block_points.size()-1,block_points.size()-1);

        	ite_gpu<1> ite;

        	ite.wthr.x = indexBuffer.size();
        	ite.wthr.y = 1;
        	ite.wthr.z = 1;
        	ite.thr.x = getBlockSize();
        	ite.thr.y = 1;
        	ite.thr.z = 1;

        	CUDA_LAUNCH((SparseGridGpuKernels::calc_exist_points<BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask>),
        				 ite,
        				 dataBuffer.toKernel(),
        				 block_points.toKernel());

        	// than we scan
        	openfpm::scan((indexT *)block_points.template getDeviceBuffer<0>(),block_points.size(),(indexT *)block_points.template getDeviceBuffer<0>(),context);

        	// Get the total number of points
        	block_points.template deviceToHost<0>(block_points.size()-1,block_points.size()-1);
        	size_t tot = block_points.template get<0>(block_points.size()-1);
        	e_points.resize(tot);

        	// we fill e_points
        	CUDA_LAUNCH((SparseGridGpuKernels::fill_e_points<BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask>),ite,
        				 dataBuffer.toKernel(),
        				 block_points.toKernel(),
        				 e_points.toKernel())

        }

        cudaDeviceSynchronize();
    }

    template<typename NNtype = NNStar<dim>>
    void findNeighbours()
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();

        const unsigned int numBlocks = indexBuffer.size();
        const unsigned int numScalars = numBlocks * NNtype::nNN;
        nn_blocks.resize(numScalars);

        if (numScalars == 0) return;

        // NOTE: Here we want to work only on one data chunk per block!

        unsigned int localThreadBlockSize = NNtype::nNN;

        unsigned int threadGridSize = numScalars % localThreadBlockSize == 0
                                      ? numScalars / localThreadBlockSize
                                      : 1 + numScalars / localThreadBlockSize;

        CUDA_LAUNCH_DIM3((SparseGridGpuKernels::findNeighbours<dim,NNtype>),
                threadGridSize, localThreadBlockSize,indexBuffer.toKernel(), this->toKernel(),nn_blocks.toKernel());
    }

    size_t countExistingElements() const
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        constexpr unsigned int pMask = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask;
        typedef typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT BAggregateT;
        typedef BlockTypeOf<BAggregateT, pMask> MaskBlockT;
        constexpr unsigned int blockSize = MaskBlockT::size;
        const auto bufferSize = indexBuffer.size();

        size_t numExistingElements = 0;

        for (size_t blockId=0; blockId<bufferSize; ++blockId)
        {
            auto dataBlock = dataBuffer.get(blockId); // Avoid binary searches as much as possible
            for (size_t elementId=0; elementId<blockSize; ++elementId)
            {
                const auto curMask = dataBlock.template get<pMask>()[elementId];

                if (this->exist(curMask))
                {
                    ++numExistingElements;
                }
            }
        }

        return numExistingElements;
    }

    size_t countBoundaryElements()
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        constexpr unsigned int pMask = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask;
        typedef typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT BAggregateT;
        typedef BlockTypeOf<BAggregateT, pMask> MaskBlockT;
        constexpr unsigned int blockSize = MaskBlockT::size;
        const auto bufferSize = indexBuffer.size();

        size_t numBoundaryElements = 0;

        for (size_t blockId=0; blockId<bufferSize; ++blockId)
        {
            auto dataBlock = dataBuffer.get(blockId); // Avoid binary searches as much as possible
            for (size_t elementId=0; elementId<blockSize; ++elementId)
            {
                const auto curMask = dataBlock.template get<pMask>()[elementId];

                if (this->exist(curMask) && this->isPadding(curMask))
                {
                    ++numBoundaryElements;
                }
            }
        }

        return numBoundaryElements;
    }

    // Also count mean+stdDev of occupancy of existing blocks
    void measureBlockOccupancyMemory(double &mean, double &deviation)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        constexpr unsigned int pMask = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask;
        typedef typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT BAggregateT;
        typedef BlockTypeOf<BAggregateT, pMask> MaskBlockT;
        constexpr unsigned int blockSize = MaskBlockT::size;
        const auto bufferSize = indexBuffer.size();

        openfpm::vector<double> measures;

        for (size_t blockId=0; blockId<bufferSize; ++blockId)
        {
            auto dataBlock = dataBuffer.get(blockId); // Avoid binary searches as much as possible
            size_t numElementsInBlock = 0;
            for (size_t elementId=0; elementId<blockSize; ++elementId)
            {
                const auto curMask = dataBlock.template get<pMask>()[elementId];

                if (this->exist(curMask))
                {
                    ++numElementsInBlock;
                }
            }
            double blockOccupancy = static_cast<double>(numElementsInBlock)/blockSize;
            measures.add(blockOccupancy);
        }

        standard_deviation(measures, mean, deviation);
    }

    // Also count mean+stdDev of occupancy of existing blocks
    void measureBlockOccupancy(double &mean, double &deviation)
    {
        // Here it is crucial to use "auto &" as the type, as we need to be sure to pass the reference to the actual buffers!
        auto & indexBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
        auto & dataBuffer = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();

        constexpr unsigned int pMask = BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask;
        typedef typename BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::AggregateInternalT BAggregateT;
        typedef BlockTypeOf<BAggregateT, pMask> MaskBlockT;
        constexpr unsigned int blockSize = MaskBlockT::size;
        const auto bufferSize = indexBuffer.size();

        openfpm::vector<double> measures;

        for (size_t blockId=0; blockId<bufferSize; ++blockId)
        {
            auto dataBlock = dataBuffer.get(blockId); // Avoid binary searches as much as possible
            size_t numElementsInBlock = 0;
            for (size_t elementId=0; elementId<blockSize; ++elementId)
            {
                const auto curMask = dataBlock.template get<pMask>()[elementId];

                if (this->exist(curMask) && !this->isPadding(curMask))
                {
                    ++numElementsInBlock;
                }
            }
            double blockOccupancy = static_cast<double>(numElementsInBlock)/blockSize;
            measures.add(blockOccupancy);
        }

        standard_deviation(measures, mean, deviation);
    }

    template<typename stencil, typename... Args>
    void applyStencilsHost(StencilMode mode = STENCIL_MODE_INPLACE, Args... args)
    {
        // Apply the given stencil on all elements which are not boundary-tagged
        // The idea is to have this function launch a __global__ function (made by us) on all existing blocks
        // then this kernel checks if elements exist && !padding and on those it calls the user-provided
        // __device__ functor. The mode of the stencil application is used basically to choose how we load the block
        // that we pass to the user functor as storeBlock: in case of Insert, we get the block through an insert (and
        // we also call the necessary aux functions); in case of an In-place we just get the block from the data buffer.
        switch (mode)
        {
            case STENCIL_MODE_INPLACE:
                applyStencilInPlaceHost<stencil>(args...);
                break;
            case STENCIL_MODE_INSERT:
//                applyStencilInsertHost<stencil>(args...);
                std::cout << "No insert mode stencil application available on Host!" << std::endl;
                break;
        }
    }
    template<typename stencil1, typename stencil2, typename ... otherStencils, typename... Args>
    void applyStencilsHost(StencilMode mode = STENCIL_MODE_INSERT, Args... args)
    {
        applyStencilsHost<stencil1>(mode, args...);
        applyStencilsHost<stencil2, otherStencils ...>(mode, args...);
    }

    //todo: Move implems into a functor for compile time choice of stencil mode
    template<typename stencil, typename... Args>
    void applyStencils(StencilMode mode = STENCIL_MODE_INSERT, Args... args)
    {
        // Apply the given stencil on all elements which are not boundary-tagged
        // The idea is to have this function launch a __global__ function (made by us) on all existing blocks
        // then this kernel checks if elements exist && !padding and on those it calls the user-provided
        // __device__ functor. The mode of the stencil application is used basically to choose how we load the block
        // that we pass to the user functor as storeBlock: in case of Insert, we get the block through an insert (and
        // we also call the necessary aux functions); in case of an In-place we just get the block from the data buffer.
        switch (mode)
        {
            case STENCIL_MODE_INPLACE:
                applyStencilInPlace<stencil>(mode,args...);
                break;
            case STENCIL_MODE_INSERT:
                applyStencilInsert<stencil>(mode,args...);
                break;
            case STENCIL_MODE_INSERT_NOFLUSH:
                applyStencilInsert<stencil>(mode,args...);
                break;
            case STENCIL_MODE_INPLACE_NO_SHARED:
                applyStencilInPlaceNoShared<stencil>(mode,args...);
                break;
        }
    }
    template<typename stencil1, typename stencil2, typename ... otherStencils, typename... Args>
    void applyStencils(StencilMode mode = STENCIL_MODE_INSERT, Args... args)
    {
        applyStencils<stencil1>(mode, args...);
        applyStencils<stencil2, otherStencils ...>(mode, args...);
    }

    //todo: Move implems into a functor for compile time choice of stencil mode
    template<typename stencil, typename... Args>
    void applyBoundaryStencils(Args... args)
    {
        // Apply the given stencil on all elements which are not boundary-tagged
        // The idea is to have this function launch a __global__ function (made by us) on all existing blocks
        // then this kernel checks if elements exist && !padding and on those it calls the user-provided
        // __device__ functor. The mode of the stencil application is used basically to choose how we load the block
        // that we pass to the user functor as storeBlock: in case of Insert, we get the block through an insert (and
        // we also call the necessary aux functions); in case of an In-place we just get the block from the data buffer.
        StencilMode mode = STENCIL_MODE_INPLACE;
        switch (mode)
        {
            case STENCIL_MODE_INPLACE:
                applyBoundaryStencilInPlace<stencil>(args...);
                break;
            case STENCIL_MODE_INSERT:
//                applyBoundaryStencilInsert<stencil>(args...);
                std::cout << "No insert mode stencil application available on BOUNDARY!" << std::endl;
                break;
        }
    }
    template<typename stencil1, typename stencil2, typename ... otherStencils, typename... Args>
    void applyBoundaryStencils(Args... args)
    {
        applyBoundaryStencils<stencil1>(args...);
        applyBoundaryStencils<stencil2, otherStencils ...>(args...);
    }

    template<typename BitMaskT>
    inline static bool isPadding(BitMaskT &bitMask)
    {
        return BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>
        ::getBit(bitMask, PADDING_BIT);
    }

    template<typename BitMaskT>
    inline static void setPadding(BitMaskT &bitMask)
    {
        BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>
        ::setBit(bitMask, PADDING_BIT);
    }

    template<typename BitMaskT>
    inline static void unsetPadding(BitMaskT &bitMask)
    {
        BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>
        ::unsetBit(bitMask, PADDING_BIT);
    }

    /*! \brief Linearization of  block coordinates
     *
     * \param blockCoord block coordinates
     *
     * \return the linearized index
     *
     */
    template<typename CoordT>
    inline size_t getBlockLinId(const CoordT & blockCoord) const
    {
        return gridGeometry.BlockLinId(blockCoord);
    }

    /*! \brief Insert the point on host side and flush directly
     *
     * First you have to move everything on host with deviceToHost, insertFlush and than move to GPU again
     *
     * \param grid point where to insert
     *
     * \return a reference to the data to fill
     *
     *
     */
    template<unsigned int p, typename CoordT>
    auto insertFlush(const grid_key_dx<dim,CoordT> &coord) -> ScalarTypeOf<AggregateBlockT, p> &
    {
    	// Linearized block_id
    	auto lin = gridGeometry.LinId(coord);
    	indexT block_id = lin / blockSize;
    	indexT local_id = lin % blockSize;

    	typedef BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base> BMG;

    	auto block_data = this->insertBlockFlush(block_id);
    	block_data.template get<BMG::pMask>()[local_id] = 1;

    	return block_data.template get<p>()[local_id];
    }

    template<unsigned int p>
    void print_vct_add_data()
    {
    	typedef BlockMapGpu<
    	        typename aggregate_convert<dim,blockEdgeSize,AggregateT>::type,
    	        threadBlockSize, indexT, layout_base> BMG;

    	auto & bM = BMG::blockMap.private_get_vct_add_data();
    	auto & vI = BMG::blockMap.private_get_vct_add_index();
    	bM.template deviceToHost<p>();
    	vI.template deviceToHost<0>();

    	std::cout << "vct_add_data: " << std::endl;

    	for (size_t i = 0 ; i < bM.size() ; i++)
    	{
    		std::cout << i << "  index: " << vI.template get<0>(i) << "  BlockData: " << std::endl;
    		for (size_t j = 0 ; j < blockSize ; j++)
    		{
    			std::cout << (int)bM.template get<p>(i)[j] << "  ";
    		}

    		std::cout << std::endl;
    	}
    }

    /*! \brief memory requested to pack this object
     *
     * \param req request
     *
     */
	template<int ... prp> inline
	void packRequest(size_t & req, mgpu::ofp_context_t &context) const
    {
    	ite_gpu<1> ite;

    	auto & indexBuffer = private_get_index_array();
    	auto & dataBuffer = private_get_data_array();

    	ite.wthr.x = indexBuffer.size();
    	ite.wthr.y = 1;
    	ite.wthr.z = 1;
    	ite.thr.x = getBlockSize();
    	ite.thr.y = 1;
    	ite.thr.z = 1;

    	tmp.resize(indexBuffer.size() + 1);

		// Launch a kernel that count the number of element on each chunks
    	CUDA_LAUNCH((SparseGridGpuKernels::calc_exist_points<BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask>),
    				 ite,
    				 dataBuffer.toKernel(),
    				 tmp.toKernel());

    	openfpm::scan((indexT *)tmp. template getDeviceBuffer<0>(),
    						tmp.size(), (indexT *)tmp. template getDeviceBuffer<0>(), context);

    	tmp.template deviceToHost<0>(tmp.size()-1,tmp.size()-1);

    	sparsegridgpu_pack_request<AggregateT,prp ...> spq;

    	boost::mpl::for_each_ref<boost::mpl::range_c<int,0,sizeof...                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                (prp)>>(spq);

		size_t n_pnt = tmp.template get<0>(tmp.size()-1);


		                        // 4 byte each chunks        data                      // we use short to pack the offset
		                        // for each counter
		req = sizeof(indexT) +               // byte required to pack the number
			  sizeof(indexT)*indexBuffer.size() +    // byte required to pack the chunk indexes
			  sizeof(indexT)*tmp.size() +            // byte required to pack the scan of the chunks points
			  n_pnt*(spq.point_size + 2);    // byte required to pack data + offset position
    }

	/*! \brief Calculate the size to pack part of this structure
	 *
	 * \warning this function does not return the byte immediately but it buffer the request until you call
	 *          packCalculate
	 *
	 * \warning to reset call packReset()
	 *
	 * \tparam prp set of properties to pack
	 *

	 * \param sub sub-grid to pack
	 * \param req output byte
	 *
	 */
	template<int ... prp> inline
	void packRequest(SparseGridGpu_iterator_sub<dim,self> & sub_it,
					 size_t & req) const
	{
		pack_subs.add();

		for (int i = 0 ; i < dim ; i++)
		{
			pack_subs.template get<0>(pack_subs.size()-1)[i] = sub_it.getStart().get(i);
			pack_subs.template get<1>(pack_subs.size()-1)[i] = sub_it.getStop().get(i);
		}
	}

	/*! \brief Reset the pack calculation
	 *
	 *
	 */
	void packReset()
	{
		pack_subs.clear();
	}

	/*! \brief Calculate the size of the information to pack
	 *
	 * \param req output size
	 * \param context gpu contect
	 *
	 */
	template<int ... prp> inline
	void packCalculate(size_t & req, mgpu::ofp_context_t &context)
	{
		//
	}

    /*! \brief Return a SparseGrid iterator
     *
     * \return a SparseGrid iterator
     *
     */
    decltype(self::type_of_iterator()) getIterator() const
    {
    	return decltype(self::type_of_iterator())(*this);
    }

    /*! \brief Return a SparseGrid iterator only on a sub-set of elements
     *
     * \return a SparseGrid iterator on a subset of elements
     *
     */
    decltype(self::type_of_subiterator()) getIterator(const grid_key_dx<dim> & start, const grid_key_dx<dim> & stop) const
    {
    	return decltype(self::type_of_subiterator())(*this,start,stop);
    }

    /*! \brief Return the index array of the blocks
     *
     * \return the index arrays of the blocks
     *
     */
    auto private_get_index_array() const -> decltype(BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer()) &
    {
    	return BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
    }

    /*! \brief Return the index array of the blocks
     *
     * \return the index arrays of the blocks
     *
     */
    auto private_get_data_array() -> decltype(BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer()) &
    {
    	return BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();
    }

    /*! \brief Return the index array of the blocks
     *
     * \return the index arrays of the blocks
     *
     */
    auto private_get_index_array() -> decltype(BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer()) &
    {
    	return BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getIndexBuffer();
    }

    /*! \brief Return the index array of the blocks
     *
     * \return the index arrays of the blocks
     *
     */
    auto private_get_data_array() const -> decltype(BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer()) &
    {
    	return BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::blockMap.getDataBuffer();
    }

    /*! \brief Return the index array of the blocks
     *
     * \return the index arrays of the blocks
     *
     */
    auto private_get_neighborhood_array() -> decltype(nn_blocks) &
    {
    	return nn_blocks;
    }


#if defined(OPENFPM_DATA_ENABLE_IO_MODULE) || defined(PERFORMANCE_TEST)

	/*! \brief write the sparse grid into VTK
	 *
	 * \param out VTK output
	 *
	 */
	template<typename Tw = float> bool write(const std::string & output)
	{
		file_type ft = file_type::BINARY;

		auto & bm = this->private_get_blockMap();

		auto & index = bm.getIndexBuffer();
		auto & data = bm.getDataBuffer();

		openfpm::vector<Point<dim,Tw>> tmp_pos;
		openfpm::vector<typename aggregate_add<AggregateT>::type> tmp_prp;

		// copy position and properties

		auto it = index.getIterator();

		while(it.isNext())
		{
			auto key = it.get();

			Point<dim,Tw> p;

			for (size_t i = 0 ; i < gridGeometry.getBlockSize() ; i++)
			{
				if (data.template get<BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask>(key)[i] != 0)
				{
					// Get the block index
					grid_key_dx<dim,int> keyg = gridGeometry.InvLinId(index.template get<0>(key),i);

					for (size_t k = 0 ; k < dim ; k++)
					{p.get(k) = keyg.get(k);}

					tmp_pos.add(p);

					tmp_prp.add();
					copy_prop_to_vector_block<decltype(data.get_o(key)),decltype(tmp_prp.last())>
					cp(data.get_o(key),tmp_prp.last(),key,i);

					boost::mpl::for_each_ref< boost::mpl::range_c<int,0,AggregateT::max_prop> >(cp);

					tmp_prp.last().template get<AggregateT::max_prop>() = data.template get<BlockMapGpu<AggregateInternalT, threadBlockSize, indexT, layout_base>::pMask>(key)[i];
				}
			}

			++it;
		}

		// VTKWriter for a set of points
		VTKWriter<boost::mpl::pair<openfpm::vector<Point<dim,Tw>>, openfpm::vector<typename aggregate_add<AggregateT>::type>>, VECTOR_POINTS> vtk_writer;
		vtk_writer.add(tmp_pos,tmp_prp,tmp_pos.size());

		openfpm::vector<std::string> prp_names;

		// Write the VTK file
		return vtk_writer.write(output,prp_names,"sparse_grid","",ft);
	}

#endif
};

template<unsigned int dim,
		 typename AggregateT,
		 unsigned int blockEdgeSize = default_edge<dim>::type::value,
		 unsigned int threadBlockSize = 128,
		 typename indexT=long int,
		 template<typename> class layout_base=memory_traits_inte,
		 typename linearizer = grid_zmb<dim, blockEdgeSize>>
using SparseGridGpu_z = SparseGridGpu<dim,AggregateT,blockEdgeSize,threadBlockSize,indexT,layout_base,linearizer>;

#endif //OPENFPM_PDATA_SPARSEGRIDGPU_HPP
