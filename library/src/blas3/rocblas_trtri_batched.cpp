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
    constexpr char rocblas_trtri_name<float>[] = "rocblas_strtri_batched";
    template <>
    constexpr char rocblas_trtri_name<double>[] = "rocblas_dtrtri_batched";
    template <>
    constexpr char rocblas_trtri_name<rocblas_float_complex>[] = "rocblas_ctrtri_batched";
    template <>
    constexpr char rocblas_trtri_name<rocblas_double_complex>[] = "rocblas_ztrtri_batched";

    template <rocblas_int NB, typename T>
    rocblas_status rocblas_trtri_batched_impl(rocblas_handle   handle,
                                              rocblas_fill     uplo,
                                              rocblas_diagonal diag,
                                              rocblas_int      n,
                                              const T* const   A[],
                                              rocblas_int      lda,
                                              T* const         invA[],
                                              rocblas_int      ldinvA,
                                              rocblas_int      batch_count)
    {
        if(!handle)
            return rocblas_status_invalid_handle;

        // Compute the optimal size for temporary device memory
        size_t els   = rocblas_internal_trtri_temp_size<NB>(n, 1);
        size_t size  = els * batch_count * sizeof(T);
        size_t sizep = batch_count * sizeof(T*);
        if(handle->is_device_memory_size_query())
        {
            if(n <= NB || !batch_count)
                return rocblas_status_size_unchanged;
            return handle->set_optimal_device_memory_size(size, sizep);
        }

        auto layer_mode = handle->layer_mode;
        if(layer_mode & rocblas_layer_mode_log_trace)
            log_trace(
                handle, rocblas_trtri_name<T>, uplo, diag, n, A, lda, invA, ldinvA, batch_count);

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
                        "ldinvA",
                        ldinvA,
                        "batch_count",
                        batch_count);

        rocblas_status arg_status
            = rocblas_trtri_arg_check(handle, uplo, diag, n, A, lda, invA, ldinvA, batch_count);
        if(arg_status != rocblas_status_continue)
            return arg_status;

        rocblas_status status;
        if(n <= NB)
        {
            status = rocblas_trtri_small<NB, T>(
                handle, uplo, diag, n, A, 0, lda, 0, 0, invA, 0, ldinvA, 0, 0, batch_count, 1);
        }
        else
        {
            // Allocate memory
            auto w_mem = handle->device_malloc(size, sizep);
            if(!w_mem)
            {
                return rocblas_status_memory_error;
            }
            void* w_C_tmp     = w_mem[0];
            void* w_C_tmp_arr = w_mem[1];

            auto w_C_tmp_host = std::make_unique<T*[]>(batch_count);
            for(int b = 0; b < batch_count; b++)
            {
                w_C_tmp_host[b] = (T*)w_C_tmp + b * els;
            }

            RETURN_IF_HIP_ERROR(hipMemcpyAsync(w_C_tmp_arr,
                                               &w_C_tmp_host[0],
                                               batch_count * sizeof(T*),
                                               hipMemcpyHostToDevice,
                                               handle->get_stream()));

            status = rocblas_trtri_large<NB, true, false, T>(handle,
                                                             uplo,
                                                             diag,
                                                             n,
                                                             A,
                                                             0,
                                                             lda,
                                                             0,
                                                             0,
                                                             invA,
                                                             0,
                                                             ldinvA,
                                                             0,
                                                             0,
                                                             batch_count,
                                                             1,
                                                             (T* const*)w_C_tmp_arr);
        }

        return status;
    }

}

extern "C" {
rocblas_status rocblas_strtri_batched(rocblas_handle     handle,
                                      rocblas_fill       uplo,
                                      rocblas_diagonal   diag,
                                      rocblas_int        n,
                                      const float* const A[],
                                      rocblas_int        lda,
                                      float* const       invA[],
                                      rocblas_int        ldinvA,
                                      rocblas_int        batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_batched_impl<NB>(handle, uplo, diag, n, A, lda, invA, ldinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

rocblas_status rocblas_dtrtri_batched(rocblas_handle      handle,
                                      rocblas_fill        uplo,
                                      rocblas_diagonal    diag,
                                      rocblas_int         n,
                                      const double* const A[],
                                      rocblas_int         lda,
                                      double* const       invA[],
                                      rocblas_int         ldinvA,
                                      rocblas_int         batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_batched_impl<NB>(handle, uplo, diag, n, A, lda, invA, ldinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

rocblas_status rocblas_ctrtri_batched(rocblas_handle                     handle,
                                      rocblas_fill                       uplo,
                                      rocblas_diagonal                   diag,
                                      rocblas_int                        n,
                                      const rocblas_float_complex* const A[],
                                      rocblas_int                        lda,
                                      rocblas_float_complex* const       invA[],
                                      rocblas_int                        ldinvA,
                                      rocblas_int                        batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_batched_impl<NB>(handle, uplo, diag, n, A, lda, invA, ldinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

rocblas_status rocblas_ztrtri_batched(rocblas_handle                      handle,
                                      rocblas_fill                        uplo,
                                      rocblas_diagonal                    diag,
                                      rocblas_int                         n,
                                      const rocblas_double_complex* const A[],
                                      rocblas_int                         lda,
                                      rocblas_double_complex* const       invA[],
                                      rocblas_int                         ldinvA,
                                      rocblas_int                         batch_count)
try
{
    constexpr rocblas_int NB = 16;
    return rocblas_trtri_batched_impl<NB>(handle, uplo, diag, n, A, lda, invA, ldinvA, batch_count);
}
catch(...)
{
    return exception_to_rocblas_status();
}

} // extern "C"
