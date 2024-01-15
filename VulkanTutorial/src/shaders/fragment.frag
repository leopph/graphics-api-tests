#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform texture2D tex;
layout(set = 0, binding = 2) uniform sampler texSampler;

void main() {
	outColor = vec4(fragColor * texture(sampler2D(tex, texSampler), uv).rgb, 1);
}
