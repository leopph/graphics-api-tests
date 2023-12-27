#include "constant_buffer.hlsli"

Buffer<float2> g_buffers[] : register(t0, space0);

float4 main(const uint vertex_id : SV_VertexID) : SV_POSITION {
  return float4(g_buffers[g_desc_indices.vertex_buffer_idx][vertex_id], 0, 1);
}