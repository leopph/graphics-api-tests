#include "constant_buffer.hlsli"

Texture2D g_textures[] : register(t0, space1);

float4 main() : SV_Target {
  return g_textures[g_desc_indices.texture_idx].Load(int3(0, 0, 0));
}