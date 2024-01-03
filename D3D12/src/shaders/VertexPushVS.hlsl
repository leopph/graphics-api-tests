struct VertexInput {
    float2 position_os : POSITION;
};

float4 main(const VertexInput vertex_input) : SV_Position {
    return float4(vertex_input.position_os, 0, 1);
}
