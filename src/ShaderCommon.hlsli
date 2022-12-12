cbuffer colorBuffer : register(b0) {
	float4 objectColor;
};

cbuffer offsetBuffer : register(b0) {
	float offsetX;
}

float4 vs_main(float2 pos : POS) : SV_POSITION {
	return  float4(pos + float2(offsetX, 0), 0.0f, 1.0f);
}

float4 ps_main() : SV_TARGET {
	return objectColor;
}