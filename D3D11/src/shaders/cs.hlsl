#include "interop.h"

RWTexture2D<float4> tex;

[numthreads(COMPUTE_SHADER_THREAD_COUNT_X, COMPUTE_SHADER_THREAD_COUNT_Y, COMPUTE_SHADER_THREAD_COUNT_Z)]
void main(const uint3 dispatch_thread_id : SV_DispatchThreadID) {
  tex[dispatch_thread_id.xy] = square_color;
}
