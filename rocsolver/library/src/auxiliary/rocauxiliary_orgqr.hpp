/************************************************************************
 * Derived from the BSD3-licensed
 * LAPACK routine (version 3.7.0) --
 *     Univ. of Tennessee, Univ. of California Berkeley,
 *     Univ. of Colorado Denver and NAG Ltd..
 *     December 2016
 * Copyright 2019-2020 Advanced Micro Devices, Inc.
 * ***********************************************************************/

#ifndef ROCLAPACK_ORGQR_HPP
#define ROCLAPACK_ORGQR_HPP

#include <hip/hip_runtime.h>
#include "rocblas.hpp"
#include "rocsolver.h"
#include "common_device.hpp"
#include "ideal_sizes.hpp"
#include "../auxiliary/rocauxiliary_org2r.hpp"
#include "../auxiliary/rocauxiliary_larfb.hpp"
#include "../auxiliary/rocauxiliary_larft.hpp"

template <typename T, typename U>
__global__ void set_zero_col(const rocblas_int n, const rocblas_int kk, U A,
                         const rocblas_int shiftA, const rocblas_int lda, const rocblas_stride strideA)
{
    const auto blocksizex = hipBlockDim_x;
    const auto blocksizey = hipBlockDim_y;
    const auto b = hipBlockIdx_z;
    const auto j = hipBlockIdx_y * blocksizey + hipThreadIdx_y + kk;
    const auto i = hipBlockIdx_x * blocksizex + hipThreadIdx_x;

    if (i < kk && j < n) {
        T *Ap = load_ptr_batch<T>(A,b,shiftA,strideA);
        
        Ap[i + j*lda] = 0.0;
    }
}

template <bool BATCHED, bool STRIDED, typename T, typename U>
rocblas_status rocsolver_orgqr_template(rocblas_handle handle, const rocblas_int m, 
                                   const rocblas_int n, const rocblas_int k, U A, const rocblas_int shiftA, 
                                   const rocblas_int lda, const rocblas_stride strideA, T* ipiv, 
                                   const rocblas_stride strideP, const rocblas_int batch_count)
{
    // quick return
    if (!n || !m || !batch_count)
        return rocblas_status_success;

    hipStream_t stream;
    rocblas_get_stream(handle, &stream);
    
    // if the matrix is small, use the unblocked variant of the algorithm
    if (k <= GEQRF_GEQR2_SWITCHSIZE) 
        return rocsolver_org2r_template<T>(handle, m, n, k, A, shiftA, lda, strideA, ipiv, strideP, batch_count);

    // (TODO) THIS SHOULD BE DONE WITH THE HANDLE MEMORY ALLOCATOR
    //memory in GPU (workspace)
    T* work;
    rocblas_int ldw = GEQRF_GEQR2_BLOCKSIZE;
    rocblas_stride strideW = rocblas_stride(ldw) *ldw;
    hipMalloc(&work, sizeof(T)*strideW*batch_count);

    // start of first blocked block
    rocblas_int jb = GEQRF_GEQR2_BLOCKSIZE;
    rocblas_int j = ((k - GEQRF_GEQR2_SWITCHSIZE - 1) / jb) * jb;
    
    // start of the unblocked block
    rocblas_int kk = min(k, j + jb); 

    rocblas_int blocksy, blocksx;
    
    // compute the unblockled part and set to zero the 
    // corresponding top submatrix
    if (kk < n) {
        blocksx = (kk - 1)/32 + 1;
        blocksy = (n- kk - 1)/32 + 1;
        hipLaunchKernelGGL(set_zero_col<T>,dim3(blocksx,blocksy,batch_count),dim3(32,32),0,stream,
                           n,kk,A,shiftA,lda,strideA);
        
        rocsolver_org2r_template<T>(handle, m - kk, n - kk, k - kk, 
                                    A, shiftA + idx2D(kk, kk, lda), lda, 
                                    strideA, (ipiv + kk), strideP, batch_count);
    }

    // compute the blocked part
    while (j >= 0) {
        
        // first update the already computed part
        // applying the current block reflector using larft + larfb
        if (j + jb < n) {
            rocsolver_larft_template<T>(handle, rocblas_forward_direction, 
                                        rocblas_column_wise, m-j, jb, 
                                        A, shiftA + idx2D(j,j,lda), lda, strideA, 
                                        (ipiv + j), strideP,
                                        work, ldw, strideW, batch_count);

            rocsolver_larfb_template<BATCHED,STRIDED,T>(handle,rocblas_side_left,rocblas_operation_none,rocblas_forward_direction,
                                        rocblas_column_wise,m-j, n-j-jb, jb,
                                        A, shiftA + idx2D(j,j,lda), lda, strideA,
                                        work, 0, ldw, strideW,
                                        A, shiftA + idx2D(j,j+jb,lda), lda, strideA, batch_count);
        }

        // now compute the current block and set to zero
        // the corresponding top submatrix
        if (j > 0) {
            blocksx = (j - 1)/32 + 1;
            blocksy = (jb - 1)/32 + 1;
            hipLaunchKernelGGL(set_zero_col<T>,dim3(blocksx,blocksy,batch_count),dim3(32,32),0,stream,
                               j+jb,j,A,shiftA,lda,strideA);
        }
        rocsolver_org2r_template<T>(handle, m - j, jb, jb, 
                                    A, shiftA + idx2D(j, j, lda), lda, 
                                    strideA, (ipiv + j), strideP, batch_count);

        j -= jb;
    }
 
    hipFree(work);

    return rocblas_status_success;
}

#endif
