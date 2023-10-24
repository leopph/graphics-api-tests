Texture2D gTex : register(t0, space0);

float4 main() : SV_TARGET {
  Texture2D tex = ResourceDescriptorHeap[0];
  return tex.Load(int3(0, 0, 0));
}