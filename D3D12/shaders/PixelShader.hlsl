float4 main() : SV_TARGET {
  Texture2D tex = ResourceDescriptorHeap[1];
  return tex.Load(int3(0, 0, 0));
}
