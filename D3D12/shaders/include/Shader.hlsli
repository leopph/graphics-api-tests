#ifndef SHADER_HLSLI
#define SHADER_HLSLI

float4 VSMain(float2 vertPos : VERTEXPOS) : SV_POSITION {
    return float4(vertPos, 0, 1);
}

float4 PSMain() : SV_TARGET {
    return float4(1, 1, 1, 1);
}

#endif