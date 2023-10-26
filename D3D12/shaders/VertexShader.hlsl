#include "constant-buffer.hlsli"

float4 main(const uint vertexId : SV_VertexID) : SV_Position {
  StructuredBuffer<float2> positions = ResourceDescriptorHeap[gDescIndices.vbIdx];
  return float4(positions[vertexId], 0, 1);
}
