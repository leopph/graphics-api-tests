#version 460 core
#extension GL_ARB_bindless_texture : require

layout(location = 0) in vec3 inFragColor;

layout(location = 0) out vec3 outFragColor;

layout(std140, binding = 0) uniform UniformBuffer
{
	sampler1D tex;
	float gammaInv;
} uUniforms;


void main()
{
	outFragColor = pow(inFragColor, vec3(uUniforms.gammaInv));
}