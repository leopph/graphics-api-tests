#include "constant_buffer.hlsli"

float4 main() : SV_TARGET {
  Texture2D tex = ResourceDescriptorHeap[g_desc_indices.texture_idx];
  return tex.Load(int3(0, 0, 0));
}
