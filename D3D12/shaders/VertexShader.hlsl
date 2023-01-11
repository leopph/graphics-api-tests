float4 VSMain(float2 vertPos : VERTEXPOS) : SV_POSITION {
    return float4(vertPos, 0, 1);
}