/* ************************************************************************
 * Copyright 2018-2020 Advanced Micro Devices, Inc.
 * ************************************************************************ */

#include "bytes.hpp"
#include "cblas_interface.hpp"
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

template <typename T>
void testing_nrm2_bad_arg(const Arguments& arg)
{
    const bool FORTRAN         = arg.fortran;
    auto       rocblas_nrm2_fn = FORTRAN ? rocblas_nrm2<T, true> : rocblas_nrm2<T, false>;

    rocblas_int         N         = 100;
    rocblas_int         incx      = 1;
    static const size_t safe_size = 100;

    rocblas_local_handle handle;

    device_vector<T>         dx(safe_size);
    device_vector<real_t<T>> d_rocblas_result(1);
    CHECK_DEVICE_ALLOCATION(dx.memcheck());
    CHECK_DEVICE_ALLOCATION(d_rocblas_result.memcheck());

    CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_device));

    EXPECT_ROCBLAS_STATUS(rocblas_nrm2_fn(handle, N, nullptr, incx, d_rocblas_result),
                          rocblas_status_invalid_pointer);
    EXPECT_ROCBLAS_STATUS(rocblas_nrm2_fn(handle, N, dx, incx, nullptr),
                          rocblas_status_invalid_pointer);
    EXPECT_ROCBLAS_STATUS(rocblas_nrm2_fn(nullptr, N, dx, incx, d_rocblas_result),
                          rocblas_status_invalid_handle);
}

template <typename T>
void testing_nrm2(const Arguments& arg)
{
    const bool FORTRAN         = arg.fortran;
    auto       rocblas_nrm2_fn = FORTRAN ? rocblas_nrm2<T, true> : rocblas_nrm2<T, false>;

    rocblas_int N    = arg.N;
    rocblas_int incx = arg.incx;

    real_t<T> rocblas_result_1;
    real_t<T> rocblas_result_2;
    real_t<T> cpu_result;

    double rocblas_error_1;
    double rocblas_error_2;

    rocblas_local_handle handle;

    // check to prevent undefined memory allocation error
    if(N <= 0 || incx <= 0)
    {
        host_vector<real_t<T>> res(1);
        CHECK_HIP_ERROR(res.memcheck());
        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_host));
        CHECK_ROCBLAS_ERROR(rocblas_nrm2_fn(handle, N, nullptr, incx, res));
        return;
    }

    size_t size_x = N * size_t(incx);

    // allocate memory on device
    device_vector<T>         dx(size_x);
    device_vector<real_t<T>> d_rocblas_result_2(1);
    CHECK_DEVICE_ALLOCATION(dx.memcheck());
    CHECK_DEVICE_ALLOCATION(d_rocblas_result_2.memcheck());

    // Naming: dx is in GPU (device) memory. hx is in CPU (host) memory, plz follow this practice
    host_vector<T> hx(size_x);

    // Initial Data on CPU
    rocblas_seedrand();
    rocblas_init<T>(hx, 1, N, incx);

    // copy data from CPU to device, does not work for incx != 1
    CHECK_HIP_ERROR(hipMemcpy(dx, hx, sizeof(T) * N * incx, hipMemcpyHostToDevice));

    double gpu_time_used, cpu_time_used;

    if(arg.unit_check || arg.norm_check)
    {
        // GPU BLAS, rocblas_pointer_mode_host
        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_host));
        CHECK_ROCBLAS_ERROR(rocblas_nrm2_fn(handle, N, dx, incx, &rocblas_result_1));

        // GPU BLAS, rocblas_pointer_mode_device
        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_device));
        CHECK_ROCBLAS_ERROR(rocblas_nrm2_fn(handle, N, dx, incx, d_rocblas_result_2));
        CHECK_HIP_ERROR(hipMemcpy(
            &rocblas_result_2, d_rocblas_result_2, sizeof(real_t<T>), hipMemcpyDeviceToHost));

        // CPU BLAS
        cpu_time_used = get_time_us_no_sync();
        cblas_nrm2<T>(N, hx, incx, &cpu_result);
        cpu_time_used = get_time_us_no_sync() - cpu_time_used;

        //      allowable error is sqrt of precision. This is based on nrm2 calculating the
        //      square root of a sum. It is assumed that the sum will have accuracy =approx=
        //      precision, so nrm2 will have accuracy =approx= sqrt(precision)
        real_t<T> abs_error
            = pow(10.0, -(std::numeric_limits<real_t<T>>::digits10 / 2.0)) * cpu_result;
        real_t<T> tolerance = 2.0; //  accounts for rounding in reduction sum. depends on n.
            //  If test fails, try decreasing n or increasing tolerance.
        abs_error *= tolerance;
        if(arg.unit_check)
        {
            near_check_general<real_t<T>, real_t<T>>(
                1, 1, 1, &cpu_result, &rocblas_result_1, abs_error);
            near_check_general<real_t<T>, real_t<T>>(
                1, 1, 1, &cpu_result, &rocblas_result_2, abs_error);
        }

        if(arg.norm_check)
        {
            rocblas_cout << "cpu=" << cpu_result << ", gpu_host_ptr=" << rocblas_result_1
                         << ", gpu_dev_ptr=" << rocblas_result_2 << "\n";
            rocblas_error_1 = std::abs((cpu_result - rocblas_result_1) / cpu_result);
            rocblas_error_2 = std::abs((cpu_result - rocblas_result_2) / cpu_result);
        }
    }

    if(arg.timing)
    {
        int number_cold_calls = arg.cold_iters;
        int number_hot_calls  = arg.iters;
        CHECK_ROCBLAS_ERROR(rocblas_set_pointer_mode(handle, rocblas_pointer_mode_device));

        for(int iter = 0; iter < number_cold_calls; iter++)
        {
            rocblas_nrm2_fn(handle, N, dx, incx, d_rocblas_result_2);
        }

        hipStream_t stream;
        CHECK_ROCBLAS_ERROR(rocblas_get_stream(handle, &stream));
        gpu_time_used = get_time_us_sync(stream); // in microseconds

        for(int iter = 0; iter < number_hot_calls; iter++)
        {
            rocblas_nrm2_fn(handle, N, dx, incx, d_rocblas_result_2);
        }

        gpu_time_used = (get_time_us_sync(stream) - gpu_time_used) / number_hot_calls;

        ArgumentModel<e_N, e_incx>{}.log_args<T>(rocblas_cout,
                                                 arg,
                                                 gpu_time_used,
                                                 nrm2_gflop_count<T>(N),
                                                 nrm2_gbyte_count<T>(N),
                                                 cpu_time_used,
                                                 rocblas_error_1,
                                                 rocblas_error_2);
    }
}
