#version 460 core
#extension GL_ARB_bindless_texture : require

layout(location = 0) in vec2 inPos;
layout(location = 1) in int inTexelIndex;
layout(location = 2) in mat4 inModelMat;

layout(location = 0) out vec3 outVertColor;

layout(std140, binding = 0) uniform UniformBuffer
{
	sampler1D tex;
	float gammaInv;
} uUniforms;


void main()
{
	gl_Position = inModelMat * vec4(inPos, 0, 1);
	outVertColor = texelFetch(uUniforms.tex, inTexelIndex, 0).rgb;
}
