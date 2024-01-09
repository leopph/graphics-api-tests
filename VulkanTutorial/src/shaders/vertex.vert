#version 450
#extension GL_GOOGLE_include_directive : enable

#include "interop.h"

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 outUv;

void main() {
    gl_Position = kUbo.proj * kUbo.view * kUbo.model * vec4(inPosition, 0, 1);
    fragColor = inColor;
    outUv = inUv;
}
