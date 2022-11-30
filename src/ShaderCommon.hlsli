cbuffer constants : register(b0)
{
    float4 color;
	float2 offset;
};

float4 vs_main(float2 pos : POS) : SV_POSITION
{
    return  float4(pos + offset, 0.0f, 1.0f);
}

float4 ps_main() : SV_TARGET
{
    return color;
}