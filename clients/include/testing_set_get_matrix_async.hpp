/* ************************************************************************
 * Copyright (C) 2018-2024 Advanced Micro Devices, Inc. All rights reserved.
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

#pragma once

#include "testing_common.hpp"

template <typename T>
void testing_set_get_matrix_async(const Arguments& arg)
{
    auto rocblas_set_matrix_async_fn    = rocblas_set_matrix_async;
    auto rocblas_set_matrix_async_fn_64 = rocblas_set_matrix_async_64;
    auto rocblas_get_matrix_async_fn    = rocblas_get_matrix_async;
    auto rocblas_get_matrix_async_fn_64 = rocblas_get_matrix_async_64;

    int64_t rows = arg.M;
    int64_t cols = arg.N;
    int64_t lda  = arg.lda;
    int64_t ldb  = arg.ldb;
    int64_t ldd  = arg.ldd;

    rocblas_local_handle handle{arg};

    hipStream_t stream;
    rocblas_get_stream(handle, &stream);

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    bool invalidGPUMatrix = rows < 0 || cols < 0 || ldd <= 0 || ldd < rows;
    bool invalidSet       = invalidGPUMatrix || lda <= 0 || lda < rows;
    bool invalidGet       = invalidGPUMatrix || ldb <= 0 || ldb < rows;

    if(invalidSet || invalidGet)
    {
        DAPI_EXPECT(invalidSet ? rocblas_status_invalid_size : rocblas_status_invalid_pointer,
                    rocblas_set_matrix_async,
                    (rows, cols, sizeof(T), nullptr, lda, nullptr, ldd, stream));

        DAPI_EXPECT(invalidGet ? rocblas_status_invalid_size : rocblas_status_invalid_pointer,
                    rocblas_get_matrix_async,
                    (rows, cols, sizeof(T), nullptr, ldd, nullptr, ldb, stream));

        return;
    }

    // Naming: dK is in GPU (device) memory. hK is in CPU (host) memory,
    host_pinned_vector<T> hA(cols * size_t(lda)); // using vector layout to reuse pinned_vector
    host_pinned_vector<T> hB(cols * size_t(ldb));
    host_vector<T>        hB_gold(cols * size_t(ldb));

    CHECK_HIP_ERROR(hA.memcheck());
    CHECK_HIP_ERROR(hB.memcheck());
    CHECK_HIP_ERROR(hB_gold.memcheck());

    double cpu_time_used;
    double rocblas_error = 0.0;

    // allocate memory on device
    device_vector<T> dD(cols * size_t(ldd)); // vector layout
    CHECK_DEVICE_ALLOCATION(dD.memcheck());

    // Initial Data on CPU
    rocblas_seedrand();
    rocblas_init<T>(hA, rows, cols, lda);
    rocblas_init<T>(hB, rows, cols, ldb);

    if(arg.unit_check || arg.norm_check)
    {
        CHECK_HIP_ERROR(hipMemset(dD, 0, sizeof(T) * cols * ldd)); // vector layout

        DAPI_CHECK(rocblas_set_matrix_async_fn, (rows, cols, sizeof(T), hA, lda, dD, ldd, stream));
        DAPI_CHECK(rocblas_get_matrix_async_fn, (rows, cols, sizeof(T), dD, ldd, hB, ldb, stream));

        // reference calculation
        cpu_time_used = get_time_us_no_sync();

        for(size_t i2 = 0; i2 < cols; i2++)
            for(size_t i1 = 0; i1 < rows; i1++)
                hB_gold[i1 + i2 * ldb] = hA[i1 + i2 * lda];

        cpu_time_used = get_time_us_no_sync() - cpu_time_used;

        hipStreamSynchronize(stream);

        if(arg.unit_check)
        {
            unit_check_general<T>(rows, cols, ldb, hB, hB_gold);
        }

        if(arg.norm_check)
        {
            rocblas_error = norm_check_general<T>('F', rows, cols, ldb, hB, hB_gold);
        }
    }

    if(arg.timing)
    {
        double gpu_time_used;
        int    number_cold_calls = arg.cold_iters;
        int    total_calls       = number_cold_calls + arg.iters;

        hipStream_t stream;
        CHECK_ROCBLAS_ERROR(rocblas_get_stream(handle, &stream));

        for(int iter = 0; iter < total_calls; iter++)
        {
            if(iter == number_cold_calls)
                gpu_time_used = get_time_us_sync(stream);

            DAPI_DISPATCH(rocblas_set_matrix_async,
                          (rows, cols, sizeof(T), hA, lda, dD, ldd, stream));
            DAPI_DISPATCH(rocblas_get_matrix_async,
                          (rows, cols, sizeof(T), dD, ldd, hB, ldb, stream));
        }

        hipStreamSynchronize(stream);
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        ArgumentModel<e_M, e_N, e_lda, e_ldb, e_ldd>{}.log_args<T>(
            rocblas_cout,
            arg,
            gpu_time_used,
            ArgumentLogging::NA_value,
            set_get_matrix_gbyte_count<T>(rows, cols),
            cpu_time_used,
            rocblas_error);
    }
}
