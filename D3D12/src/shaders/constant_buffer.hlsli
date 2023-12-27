#ifndef CONSTANT_BUFFER_HLSLI
#define CONSTANT_BUFFER_HLSLI

struct DescriptorIndices
{
  int vertex_buffer_idx;
  int texture_idx;
  int pad0;
  int pad1;
};

ConstantBuffer<DescriptorIndices> g_desc_indices : register(b0, space0);

#endif