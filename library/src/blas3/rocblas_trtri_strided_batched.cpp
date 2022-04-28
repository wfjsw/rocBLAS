/* ************************************************************************
 * Copyright (C) 2016-2022 Advanced Micro Devices, Inc. All rights reserved.
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
 *
 * ************************************************************************ */

#include "logging.hpp"
#include "rocblas_trtri.hpp"
#include "utility.hpp"

namespace
{
    template <typename>
    constexpr char rocblas_trtri_name[] = "unknown";
    template <>
    constexpr char rocblas_trtri_name<float>[] = "rocblas_strtri_strided_batched";
    template <>
    constexpr char rocblas_trtri_name<double>[] = "rocblas_dtrtri_strided_batched";
    template <>
    constexpr char rocblas_trtri_name<rocblas_float_complex>[] = "rocblas_ctrtri_strided_batched";
    template <>
    constexpr char rocblas_trtri_name<rocblas_double_complex>[] = "rocblas_ztrtri_strided_batched";

    template <rocblas_int NB, typename T>
    rocblas_status rocblas_trtri_strided_batched_impl(rocblas_handle   handle,
                                                      rocblas_fill     uplo,
                                                      rocblas_diagonal diag,
                                                      rocblas_int      n,
                                                      const T*         A,
                                                      rocblas_int      lda,
                                                      rocblas_stride   bsa,
                                                      T*               invA,
                                                      rocblas_int      ldinvA,
                                                      rocblas_stride   bsinvA,
                                                      rocblas_int      batch_count)
    {
        if(!handle)
            return rocblas_status_invalid_handle;

        // Compute the optimal size for temporary device memory
        size_t size = rocblas_internal_trtri_temp_size<NB>(n, batch_count) * sizeof(T);
        if(handle->is_device_memory_size_query())
        {
            if(n <= NB || !batch_count)
                return rocblas_status_size_unchanged;
            return handle->set_optimal_device_memory_size(size);
        }

        auto layer_mode = handle->layer_mode;
        if(layer_mode & rocblas_layer_mode_log_trace)
            log_trace(handle,
                      rocblas_trtri_name<T>,
                      uplo,
                      diag,
                      n,
                      A,
                      lda,
                      bsa,
                      invA,
                      ldinvA,
                      bsinvA,
                      batch_count);

        if(layer_mode & rocblas_layer_mode_log_profile)
            log_profile(handle,
                        rocblas_trtri_name<T>,
                        "uplo",
                        rocblas_fill_letter(uplo),
                        "diag",
                        rocblas_diag_letter(diag),
                        "N",
                        n,
                        "lda",
                        lda,
                        "bsa",
                        bsa,
                        "ldinvA",
                        ldinvA,
                        "bsinvA",
                        bsinvA,
                        "batch_count",
                        batch_count);

        rocblas_status arg_status
            = rocblas_trtri_arg_check(handle, uplo, diag, n, A, lda, invA, ldinvA, batch_count);
        if(arg_status != rocblas_status_continue)
            return arg_status;

        rocblas_status status;
        if(n <= NB)
        {
            status = rocblas_trtri_small<NB, T>(handle,
                                                uplo,
                                                diag,
                                                n,
                                                A,
                                                0,
                                                lda,
                                                bsa,
                                                0,
                                                invA,
                                                0,
                                                ldinvA,
                                                bsinvA,
                                                0,
                                                batch_count,
                                                1);
        }
        else
        {
            // Allocate memory
            auto w_C_tmp = handle->device_malloc(size);
            if(!w_C_tmp)
                return rocblas_status_memory_error;

            status = rocblas_trtri_large<NB, false, true, T>(handle,
                                                             uplo,
                                                             diag,
                                                             n,
                                                             A,
                                                             0,
                                                             lda,
                                                             bsa,
                                                             0,
                                                             invA,
                                                             0,
                                                             ldinvA,
                                                             bsinvA,
                                                             0,
                                                             batch_count,
                                                             1,
                                                             (T*)w_C_tmp);
        }

        return status;
    }

}

/* ============================================================================================ */

/*
 * ===========================================================================
 *    C interface
 * ===========================================================================
 */

extern "C" {
rocblas_status rocblas_strtri_strided_batched(rocblas_handle   handle,
                                              rocblas_fill     uplo,
                                              rocblas_diagonal diag,
                                              rocblas_int      n,
                                              const float*     A,
                                              rocblas_int      lda,
                                              rocblas_stride   bsa,
                                              float*           invA,
                                              rocblas_int      ldinvA,
                                              rocblas_stride   bsinvA,
                                              rocblas_int      batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_strided_batched_impl<NB>(
        handle, uplo, diag, n, A, lda, bsa, invA, ldinvA, bsinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

rocblas_status rocblas_dtrtri_strided_batched(rocblas_handle   handle,
                                              rocblas_fill     uplo,
                                              rocblas_diagonal diag,
                                              rocblas_int      n,
                                              const double*    A,
                                              rocblas_int      lda,
                                              rocblas_stride   bsa,
                                              double*          invA,
                                              rocblas_int      ldinvA,
                                              rocblas_stride   bsinvA,
                                              rocblas_int      batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_strided_batched_impl<NB>(
        handle, uplo, diag, n, A, lda, bsa, invA, ldinvA, bsinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

rocblas_status rocblas_ctrtri_strided_batched(rocblas_handle               handle,
                                              rocblas_fill                 uplo,
                                              rocblas_diagonal             diag,
                                              rocblas_int                  n,
                                              const rocblas_float_complex* A,
                                              rocblas_int                  lda,
                                              rocblas_stride               bsa,
                                              rocblas_float_complex*       invA,
                                              rocblas_int                  ldinvA,
                                              rocblas_stride               bsinvA,
                                              rocblas_int                  batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_strided_batched_impl<NB>(
        handle, uplo, diag, n, A, lda, bsa, invA, ldinvA, bsinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

rocblas_status rocblas_ztrtri_strided_batched(rocblas_handle                handle,
                                              rocblas_fill                  uplo,
                                              rocblas_diagonal              diag,
                                              rocblas_int                   n,
                                              const rocblas_double_complex* A,
                                              rocblas_int                   lda,
                                              rocblas_stride                bsa,
                                              rocblas_double_complex*       invA,
                                              rocblas_int                   ldinvA,
                                              rocblas_stride                bsinvA,
                                              rocblas_int                   batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_strided_batched_impl<NB>(
        handle, uplo, diag, n, A, lda, bsa, invA, ldinvA, bsinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

} // extern "C"
