#include "interop.h"

float4 main(const float2 pos : POSITION) : SV_POSITION {
  return float4(pos * position_multiplier, 0, 1);
}
