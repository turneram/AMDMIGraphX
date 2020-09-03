#ifndef MIGRAPHX_GUARD_RTGLIB_DEVICE_SOFTMAX_HPP
#define MIGRAPHX_GUARD_RTGLIB_DEVICE_SOFTMAX_HPP

#include <migraphx/argument.hpp>
#include <migraphx/config.hpp>
#include <hip/hip_runtime_api.h>

namespace migraphx {
inline namespace MIGRAPHX_INLINE_NS {
namespace gpu {
namespace device {

void softmax(hipStream_t stream, const argument& result, const argument& arg, int64_t axis);

void mul_add_softmax(hipStream_t stream,
                     const argument& result,
                     const argument& arg1,
                     const argument& arg2,
                     const argument& arg3,
                     int64_t axis);

} // namespace device
} // namespace gpu
} // namespace MIGRAPHX_INLINE_NS
} // namespace migraphx

#endif
