#include "constant-buffer.hlsli"

float4 main() : SV_TARGET {
  Texture2D tex = ResourceDescriptorHeap[gDescIndices.texIdx];
  return tex.Load(int3(0, 0, 0));
}
