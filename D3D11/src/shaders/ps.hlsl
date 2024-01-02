#include "interop.h"

#define MAKE_TEXTURE_REGISTER(slot) t ## slot

Texture2D tex : register(MAKE_TEXTURE_REGISTER(0));

float4 main() : SV_TARGET {
  return tex.Load(int3(0, 0, 0));
}
