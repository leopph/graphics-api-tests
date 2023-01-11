float4 main(float2 vertPos : VERTEXPOS) : SV_POSITION {
    return float4(vertPos, 0, 1);
}