Texture2D g_texture : register(t1);

float4 main() : SV_Target {
  return g_texture.Load(int3(0, 0, 0));
}