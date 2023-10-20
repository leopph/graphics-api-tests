RWTexture2D<float4> gTex;

[numthreads(8, 8, 1)]
void main(const uint3 dispatchThreadId: SV_DispatchThreadID) {
  gTex[dispatchThreadId.xy] = float4(1, 0, 0, 1);
}