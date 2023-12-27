#include "constant_buffer.hlsli"

float4 main(const uint vertex_id : SV_VertexID) : SV_Position {
  Buffer<float2> positions = ResourceDescriptorHeap[g_desc_indices.vertex_buffer_idx];
  return float4(positions[vertex_id], 0, 1);
}
