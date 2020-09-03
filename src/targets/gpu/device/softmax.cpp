#include <migraphx/shape.hpp>
#include <migraphx/argument.hpp>
#include <migraphx/dfor.hpp>
#include <migraphx/gpu/device/fast_div.hpp>
#include <migraphx/gpu/device/softmax.hpp>
#include <migraphx/gpu/device/reduce.hpp>
#include <migraphx/gpu/device/tensor.hpp>
#include <migraphx/gpu/device/launch.hpp>
#include <migraphx/gpu/device/types.hpp>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace gpu {
namespace device {

template <class Batch, class In, class Out>
void softmax_impl(hipStream_t stream,
                  const Batch& batch,
                  index_int batch_item_num,
                  index_int axis,
                  In fin,
                  Out fout)
{
    const index_int max_block_size = 256;
    const index_int block_size     = compute_block_size(batch_item_num, max_block_size);
    auto block_size_div            = encode_divisor(block_size);
    gs_launch(stream, batch.elements() * block_size, block_size)([=](auto i, auto idx) __device__ {
        auto data_idx = batch.multi(fast_div(i, block_size_div));
        using type    = device_type<std::decay_t<decltype(fin(data_idx))>>;
        type init     = lowest();

        auto batch_max =
            block_reduce<max_block_size>(idx, max{}, init, batch_item_num, [&](auto j) __device__ {
                data_idx[axis] = j;
                return fin(data_idx);
            });

        auto batch_sum =
            block_reduce<max_block_size>(idx, sum{}, 0, batch_item_num, [&](auto j) __device__ {
                data_idx[axis] = j;
                auto val       = fin(data_idx) - batch_max;
                return ::exp(to_hip_type(val));
            });

        idx.local_stride(batch_item_num, [&](auto j) __device__ {
            data_idx[axis] = j;
            auto val       = fin(data_idx) - batch_max;
            fout(data_idx, ::exp(to_hip_type(val)) / batch_sum);
        });
    });
}

void mul_add_softmax(hipStream_t stream,
                     const argument& result,
                     const argument& arg1,
                     const argument& arg2,
                     const argument& arg3,
                     int64_t axis)
{
    auto batch_lens          = result.get_shape().lens();
    index_int batch_item_num = batch_lens[axis];
    batch_lens[axis]         = 1;
    migraphx::shape batch_shape{result.get_shape().type(), batch_lens};

    hip_visit_all(result, arg1, arg2, arg3, batch_shape)(
        [&](auto output, auto input1, auto input2, auto input3, auto batch) {
            auto read = [=](auto data_idx) {
                return input2[data_idx] * input1[data_idx] + input3[data_idx];
            };
            auto write = [=](auto data_idx, auto x) { output[data_idx] = x; };
            softmax_impl(stream, batch, batch_item_num, axis, read, write);
        });
}

void softmax(hipStream_t stream, const argument& result, const argument& arg, int64_t axis)
{
    auto batch_lens          = result.get_shape().lens();
    index_int batch_item_num = batch_lens[axis];
    batch_lens[axis]         = 1;
    migraphx::shape batch_shape{result.get_shape().type(), batch_lens};

    hip_visit_all(result, arg, batch_shape)([&](auto output, auto input, auto batch) {
#if 1
        auto read  = [=](auto data_idx) { return input[data_idx]; };
        auto write = [=](auto data_idx, auto x) { output[data_idx] = x; };
        softmax_impl(stream, batch, batch_item_num, axis, read, write);
#else
        const index_int max_block_size = 256;
        const index_int block_size     = compute_block_size(batch_item_num, max_block_size);
        std::cout << "block_size: " << block_size << std::endl;
        std::cout << "batch_shape.elements(): " << batch_shape.elements() << std::endl;
        std::cout << "Global: " << batch_shape.elements() * block_size << std::endl;
        gs_launch(stream,
                  batch_shape.elements() * block_size,
                  block_size)([=](auto i, auto idx) __device__ {
            auto data_idx = batch.multi(i / block_size);
            using type    = device_type<std::remove_cv_t<typename decltype(input)::value_type>>;
            type init     = lowest();

            auto batch_max = block_reduce<max_block_size>(
                idx, max{}, init, batch_item_num, [&](auto j) __device__ {
                    data_idx[axis] = j;
                    return input[data_idx];
                });

            auto batch_sum =
                block_reduce<max_block_size>(idx, sum{}, 0, batch_item_num, [&](auto j) __device__ {
                    data_idx[axis] = j;
                    auto val       = input[data_idx] - batch_max;
                    return ::exp(to_hip_type(val));
                });

            idx.local_stride(batch_item_num, [&](auto j) __device__ {
                data_idx[axis]   = j;
                auto val         = input[data_idx] - batch_max;
                output[data_idx] = ::exp(to_hip_type(val)) / batch_sum;
            });
        });
#endif
    });
}

} // namespace device
} // namespace gpu
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx
