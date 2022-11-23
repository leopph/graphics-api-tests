cbuffer constants : register(b0)
{
	float2 offset;
};

float4 vs_main(float2 pos : POS) : SV_POSITION
{
    return  float4(pos + offset, 0.0f, 1.0f);
}

float4 ps_main() : SV_TARGET
{
    return float4(0.8, 0.8, 0.8, 1);   
}