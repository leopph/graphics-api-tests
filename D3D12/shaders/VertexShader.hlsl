float4 main(const uint vertexId : SV_VertexID) : SV_Position {
  StructuredBuffer<float2> positions = ResourceDescriptorHeap[0];
  return float4(positions[vertexId], 0, 1);
}
