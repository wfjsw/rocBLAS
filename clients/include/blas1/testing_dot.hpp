/* ************************************************************************
 * Copyright (C) 2018-2023 Advanced Micro Devices, Inc. All rights reserved.
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

#include "bytes.hpp"
#include "cblas_interface.hpp"
#include "flops.hpp"
#include "near.hpp"
#include "norm.hpp"
#include "rocblas.hpp"
#include "rocblas_init.hpp"
#include "rocblas_math.hpp"
#include "rocblas_random.hpp"
#include "rocblas_test.hpp"
#include "rocblas_vector.hpp"
#include "unit.hpp"
#include "utility.hpp"

#include "blas1/rocblas_dot.hpp"

template <typename T, bool CONJ = false>
void testing_dot_bad_arg(const Arguments& arg)
{
    auto rocblas_dot_fn = arg.api == FORTRAN
                              ? (CONJ ? rocblas_dotc<T, true> : rocblas_dot<T, true>)
                              : (CONJ ? rocblas_dotc<T, false> : rocblas_dot<T, false>);

    for(auto pointer_mode : {rocblas_pointer_mode_host, rocblas_pointer_mode_device})
    {
        rocblas_local_handle handle{arg};
        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, pointer_mode));

        rocblas_int N    = 100;
        rocblas_int incx = 1;
        rocblas_int incy = 1;

        // Allocate device memory
        device_vector<T> dx(N, incx);
        device_vector<T> dy(N, incy);
        device_vector<T> d_rocblas_result(1, 1);

        // Check device memory allocation
        CHECK_DEVICE_ALLOCATION(dx.memcheck());
        CHECK_DEVICE_ALLOCATION(dy.memcheck());
        CHECK_DEVICE_ALLOCATION(d_rocblas_result.memcheck());

        // don't write to result so device pointer fine for both host and device mode

        EXPECT_ROCBLAS_STATUS((rocblas_dot_fn)(nullptr, N, dx, incx, dy, incy, d_rocblas_result),
                              rocblas_status_invalid_handle);
        EXPECT_ROCBLAS_STATUS(
            (rocblas_dot_fn)(handle, N, nullptr, incx, dy, incy, d_rocblas_result),
            rocblas_status_invalid_pointer);
        EXPECT_ROCBLAS_STATUS(
            (rocblas_dot_fn)(handle, N, dx, incx, nullptr, incy, d_rocblas_result),
            rocblas_status_invalid_pointer);
        EXPECT_ROCBLAS_STATUS((rocblas_dot_fn)(handle, N, dx, incx, dy, incy, nullptr),
                              rocblas_status_invalid_pointer);
    }
}

template <typename T>
void testing_dotc_bad_arg(const Arguments& arg)
{
    testing_dot_bad_arg<T, true>(arg);
}

template <typename T, bool CONJ = false>
void testing_dot(const Arguments& arg)
{
    auto rocblas_dot_fn = arg.api == FORTRAN
                              ? (CONJ ? rocblas_dotc<T, true> : rocblas_dot<T, true>)
                              : (CONJ ? rocblas_dotc<T, false> : rocblas_dot<T, false>);

    rocblas_int N    = arg.N;
    rocblas_int incx = arg.incx;
    rocblas_int incy = arg.incy;

    T cpu_result;
    T rocblas_result_1;
    T rocblas_result_2;

    double               rocblas_error_1;
    double               rocblas_error_2;
    bool                 HMM = arg.HMM;
    rocblas_local_handle handle{arg};

    // check to prevent undefined memmory allocation error
    if(N <= 0)
    {
        device_vector<T> d_rocblas_result(1);
        CHECK_DEVICE_ALLOCATION(d_rocblas_result.memcheck());

        host_vector<T> h_rocblas_result(1);
        CHECK_HIP_ERROR(h_rocblas_result.memcheck());

        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_device));
        CHECK_ROCBLAS_ERROR(
            (rocblas_dot_fn)(handle, N, nullptr, incx, nullptr, incy, d_rocblas_result));

        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_host));
        CHECK_ROCBLAS_ERROR(
            (rocblas_dot_fn)(handle, N, nullptr, incx, nullptr, incy, h_rocblas_result));

        T cpu_0 = T(0);
        T gpu_0, gpu_1;
        CHECK_HIP_ERROR(hipMemcpy(&gpu_0, d_rocblas_result, sizeof(T), hipMemcpyDeviceToHost));
        gpu_1 = h_rocblas_result[0];
        unit_check_general<T>(1, 1, 1, &cpu_0, &gpu_0);
        unit_check_general<T>(1, 1, 1, &cpu_0, &gpu_1);

        return;
    }

    // Naming: `h` is in CPU (host) memory(eg hx), `d` is in GPU (device) memory (eg dx).
    // Allocate host memory
    host_vector<T> hx(N, incx);
    host_vector<T> hy(N, incy);

    // Allocate device memory
    device_vector<T> dx(N, incx, HMM);
    device_vector<T> dy(N, incy, HMM);
    device_vector<T> d_rocblas_result_2(1, 1, HMM);

    // Check device memory allocation
    CHECK_DEVICE_ALLOCATION(dx.memcheck());
    CHECK_DEVICE_ALLOCATION(dy.memcheck());
    CHECK_DEVICE_ALLOCATION(d_rocblas_result_2.memcheck());

    // Initialize data on host memory
    rocblas_init_vector(hx, arg, rocblas_client_alpha_sets_nan, true);
    rocblas_init_vector(hy, arg, rocblas_client_alpha_sets_nan, false, true);

    // copy data from CPU to device, does not work for incx != 1
    CHECK_HIP_ERROR(dx.transfer_from(hx));
    CHECK_HIP_ERROR(dy.transfer_from(hy));

    double cpu_time_used;

    // arg.algo indicates to force optimized x dot x kernel algorithm with equal inc
    auto dy_ptr = (arg.algo) ? (T*)(dx) : (T*)(dy);
    auto hy_ptr = (arg.algo) ? &hx[0] : &hy[0];
    if(arg.algo)
        incy = incx;

    if(arg.unit_check || arg.norm_check)
    {
        if(arg.pointer_mode_host)
        {
            // GPU BLAS, rocblas_pointer_mode_host
            CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_host));
            CHECK_ROCBLAS_ERROR(
                (rocblas_dot_fn)(handle, N, dx, incx, dy_ptr, incy, &rocblas_result_1));
        }

        if(arg.pointer_mode_device)
        {
            // GPU BLAS, rocblas_pointer_mode_device
            CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_device));

            handle.pre_test(arg);
            if(arg.api != INTERNAL)
            {
                CHECK_ROCBLAS_ERROR(
                    (rocblas_dot_fn)(handle, N, dx, incx, dy_ptr, incy, d_rocblas_result_2));
            }
            else if constexpr(std::is_same_v<T, float>)
            {
                rocblas_stride offset_x = arg.lda;
                rocblas_stride offset_y = arg.ldb;
                CHECK_ROCBLAS_ERROR(
                    (rocblas_internal_dot_template<T, T>)(handle,
                                                          N,
                                                          dx + offset_x,
                                                          -offset_x,
                                                          incx,
                                                          arg.stride_x,
                                                          dy_ptr + offset_y,
                                                          -offset_y,
                                                          incy,
                                                          arg.stride_y,
                                                          1,
                                                          d_rocblas_result_2,
                                                          nullptr)); // N must be small
            }
            handle.post_test(arg);
        }

        // CPU BLAS
        cpu_time_used = get_time_us_no_sync();
        (CONJ ? cblas_dotc<T> : cblas_dot<T>)(N, hx, incx, hy_ptr, incy, &cpu_result);
        cpu_time_used = get_time_us_no_sync() - cpu_time_used;

        if(arg.pointer_mode_host)
        {
            if(arg.unit_check)
            {
                if(std::is_same_v<T, rocblas_half> && N > 10000)
                {
                    // For large K, rocblas_half tends to diverge proportional to K
                    // Tolerance is slightly greater than 1 / 1024.0
                    const double tol = N * sum_error_tolerance<T>;

                    near_check_general<T>(1, 1, 1, &cpu_result, &rocblas_result_1, tol);
                }
                else
                {
                    unit_check_general<T>(1, 1, 1, &cpu_result, &rocblas_result_1);
                }
            }

            if(arg.norm_check)
            {
                rocblas_error_1 = double(rocblas_abs((cpu_result - rocblas_result_1) / cpu_result));
            }
        }

        if(arg.pointer_mode_device)
        {
            CHECK_HIP_ERROR(
                hipMemcpy(&rocblas_result_2, d_rocblas_result_2, sizeof(T), hipMemcpyDeviceToHost));

            if(arg.unit_check)
            {
                if(std::is_same_v<T, rocblas_half> && N > 10000)
                {
                    // For large K, rocblas_half tends to diverge proportional to K
                    // Tolerance is slightly greater than 1 / 1024.0
                    const double tol = N * sum_error_tolerance<T>;

                    near_check_general<T>(1, 1, 1, &cpu_result, &rocblas_result_2, tol);
                }
                else
                {
                    unit_check_general<T>(1, 1, 1, &cpu_result, &rocblas_result_2);
                }
            }

            if(arg.norm_check)
            {
                rocblas_error_2 = double(rocblas_abs((cpu_result - rocblas_result_2) / cpu_result));
            }
        }
    }

    if(arg.timing)
    {
        double gpu_time_used;
        int    number_cold_calls = arg.cold_iters;
        int    number_hot_calls  = arg.iters;
        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_device));

        for(int iter = 0; iter < number_cold_calls; iter++)
        {
            (rocblas_dot_fn)(handle, N, dx, incx, dy_ptr, incy, d_rocblas_result_2);
        }

        hipStream_t stream;
        CHECK_ROCBLAS_ERROR(rocblas_get_stream(handle, &stream));
        gpu_time_used = get_time_us_sync(stream); // in microseconds

        for(int iter = 0; iter < number_hot_calls; iter++)
        {
            (rocblas_dot_fn)(handle, N, dx, incx, dy_ptr, incy, d_rocblas_result_2);
        }

        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        ArgumentModel<e_N, e_incx, e_incy, e_algo>{}.log_args<T>(rocblas_cout,
                                                                 arg,
                                                                 gpu_time_used,
                                                                 dot_gflop_count<CONJ, T>(N),
                                                                 dot_gbyte_count<T>(N),
                                                                 cpu_time_used,
                                                                 rocblas_error_1,
                                                                 rocblas_error_2);
    }
}

template <typename T>
void testing_dotc(const Arguments& arg)
{
    testing_dot<T, true>(arg);
}
