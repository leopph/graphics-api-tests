Buffer<float2> g_vertex_buffer : register(t0);

float4 main(const uint vertex_id : SV_VertexId) : SV_Position {
  return float4(g_vertex_buffer[vertex_id], 0, 1);
}