/************************************************************************
 * Derived from the BSD3-licensed
 * LAPACK routine (version 3.7.0) --
 *     Univ. of Tennessee, Univ. of California Berkeley,
 *     Univ. of Colorado Denver and NAG Ltd..
 *     December 2016
 * Copyright 2019-2020 Advanced Micro Devices, Inc.
 * ***********************************************************************/

#ifndef ROCLAPACK_ORML2_HPP
#define ROCLAPACK_ORML2_HPP

#include <hip/hip_runtime.h>
#include "rocblas.hpp"
#include "rocsolver.h"
#include "common_device.hpp"
#include "../auxiliary/rocauxiliary_larf.hpp"

template <typename T, typename U>
rocblas_status rocsolver_orml2_template(rocblas_handle handle, const rocblas_side side, const rocblas_operation trans, 
                                   const rocblas_int m, const rocblas_int n, 
                                   const rocblas_int k, U A, const rocblas_int shiftA, const rocblas_int lda, 
                                   const rocblas_stride strideA, T* ipiv, 
                                   const rocblas_stride strideP, U C, const rocblas_int shiftC, const rocblas_int ldc,
                                   const rocblas_stride strideC, const rocblas_int batch_count)
{
    // quick return
    if (!n || !m || !k || !batch_count)
        return rocblas_status_success;

    hipStream_t stream;
    rocblas_get_stream(handle, &stream);

    // (TODO) THIS SHOULD BE DONE WITH THE HANDLE MEMORY ALLOCATOR
    // memory in GPU (workspace)
    T *diag;
    hipMalloc(&diag,sizeof(T)*batch_count);

    // determine limits and indices
    bool left = (side == rocblas_side_left);
    bool transpose = (trans == rocblas_operation_transpose);
    rocblas_int start, step, ncol, nrow, ic, jc;
    if (left) {
        ncol = n;
        jc = 0;
        if (!transpose) {
            start = -1;
            step = 1;
        } else {
            start = k;
            step = -1;
        }
    } else {
        nrow = m;
        ic = 0;
        if (!transpose) {
            start = k;
            step = -1;
        } else {
            start = -1;
            step = 1;
        }
    }

    rocblas_int i;
    for (rocblas_int j = 1; j <= k; ++j) {
        i = start + step*j;    // current householder vector
        if (left) {
            nrow = m - i;
            ic = i;
        } else {
            ncol = n - i;
            jc = i;
        }
    
        // insert one in A(i,i) tobuild/apply the householder matrix 
        hipLaunchKernelGGL(set_one_diag,dim3(batch_count,1,1),dim3(1,1,1),0,stream,diag,A,shiftA+idx2D(i,i,lda),strideA);

        // Apply current Householder reflector 
        rocsolver_larf_template(handle,side,                        //side
                                nrow,                               //number of rows of matrix to modify
                                ncol,                               //number of columns of matrix to modify    
                                A, shiftA + idx2D(i,i,lda),         //householder vector x
                                lda, strideA,                       //inc of x
                                (ipiv + i), strideP,                //householder scalar (alpha)
                                C, shiftC + idx2D(ic,jc,ldc),       //matrix to work on
                                ldc, strideC,                       //leading dimension
                                batch_count);

        // restore original value of A(i,i)
        hipLaunchKernelGGL(restore_diag,dim3(batch_count,1,1),dim3(1,1,1),0,stream,diag,A,shiftA+idx2D(i,i,lda),strideA);
    }

    hipFree(diag);
 
    return rocblas_status_success;
}

#endif
