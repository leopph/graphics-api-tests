cbuffer offsetBuffer : register(b0) {
    float offset_x;
}

float4 main(const float2 pos : POSITION) : SV_POSITION {
    return float4(pos + float2(offset_x, 0), 0.0f, 1.0f);
}

