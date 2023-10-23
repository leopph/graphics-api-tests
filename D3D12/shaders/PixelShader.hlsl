Texture2D gTex : register(t0, space0);

float4 main() : SV_TARGET {
  return gTex.Load(int3(0, 0, 0));
}