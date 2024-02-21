/* ************************************************************************
 * Copyright (C) 2016-2024 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell cop-
 * ies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IM-
 * PLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNE-
 * CTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ************************************************************************ */

#include "handle.hpp"
#include "int64_helpers.hpp"

#include "blas2/rocblas_hpmv.hpp" // int32 API called

template <typename API_INT, typename TScal, typename TConstPtr, typename TPtr>
rocblas_status rocblas_hpmv_launcher_64(rocblas_handle handle,
                                        rocblas_fill   uplo,
                                        API_INT        n_64,
                                        TScal          alpha,
                                        TConstPtr      AP,
                                        rocblas_stride offseta,
                                        rocblas_stride strideA,
                                        TConstPtr      x,
                                        rocblas_stride offsetx,
                                        int64_t        incx_64,
                                        rocblas_stride stridex,
                                        TScal          beta,
                                        TPtr           y,
                                        rocblas_stride offsety,
                                        int64_t        incy_64,
                                        rocblas_stride stridey,
                                        API_INT        batch_count_64)
{
    // Quick return if possible. Not Argument error
    if(!n_64 || !batch_count_64)
        return rocblas_status_success;

    if(n_64 > c_i32_max)
        return rocblas_status_invalid_size; // defer adding new kernels for sizes exceeding practical memory

    for(int64_t b_base = 0; b_base < batch_count_64; b_base += c_i64_grid_YZ_chunk)
    {
        auto    x_ptr       = adjust_ptr_batch(x, b_base, stridex);
        auto    y_ptr       = adjust_ptr_batch(y, b_base, stridey);
        auto    A_ptr       = adjust_ptr_batch(AP, b_base, strideA);
        int32_t batch_count = int32_t(std::min(batch_count_64 - b_base, c_i64_grid_YZ_chunk));

        auto shiftA = offseta;

        rocblas_status status = rocblas_hpmv_launcher<rocblas_int>(handle,
                                                                   uplo,
                                                                   (rocblas_int)n_64,
                                                                   alpha,
                                                                   A_ptr,
                                                                   shiftA,
                                                                   strideA,
                                                                   x_ptr,
                                                                   offsetx,
                                                                   incx_64,
                                                                   stridex,
                                                                   beta,
                                                                   y_ptr,
                                                                   offsety,
                                                                   incy_64,
                                                                   stridey,
                                                                   batch_count);

        if(status != rocblas_status_success)
            return status;
    } // batch
    return rocblas_status_success;
}

// Instantiations below will need to be manually updated to match any change in
// template parameters in the files *hpmv*.cpp

#ifdef INST_HPMV_LAUNCHER
#error INST_HPMV_LAUNCHER already defined
#endif

#define INST_HPMV_LAUNCHER(TI_, TScal_, TConstPtr_, TPtr_)                            \
    template rocblas_status rocblas_hpmv_launcher_64<TI_, TScal_, TConstPtr_, TPtr_>( \
        rocblas_handle handle,                                                        \
        rocblas_fill   uplo,                                                          \
        TI_            n,                                                             \
        TScal_         alpha,                                                         \
        TConstPtr_     AP,                                                            \
        rocblas_stride offseta,                                                       \
        rocblas_stride strideA,                                                       \
        TConstPtr_     x,                                                             \
        rocblas_stride offsetx,                                                       \
        int64_t        incx,                                                          \
        rocblas_stride stridex,                                                       \
        TScal_         beta,                                                          \
        TPtr_          y,                                                             \
        rocblas_stride offsety,                                                       \
        int64_t        incy,                                                          \
        rocblas_stride stridey,                                                       \
        TI_            batch_count);

INST_HPMV_LAUNCHER(int64_t,
                   rocblas_float_complex const*,
                   rocblas_float_complex const*,
                   rocblas_float_complex*)
INST_HPMV_LAUNCHER(int64_t,
                   rocblas_double_complex const*,
                   rocblas_double_complex const*,
                   rocblas_double_complex*)

INST_HPMV_LAUNCHER(int64_t,
                   rocblas_float_complex const*,
                   rocblas_float_complex const* const*,
                   rocblas_float_complex* const*)
INST_HPMV_LAUNCHER(int64_t,
                   rocblas_double_complex const*,
                   rocblas_double_complex const* const*,
                   rocblas_double_complex* const*)

#undef INST_HPMV_LAUNCHER