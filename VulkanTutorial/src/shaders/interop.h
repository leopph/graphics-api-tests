#ifndef SHADER_INTEROP_H
#define SHADER_INTEROP_H

#ifdef __cplusplus
#include <glm/glm.hpp>

#define VEC2 glm::vec2
#define VEC3 glm::vec3
#define VEC4 glm::vec4
#define MAT2 glm::mat2
#define MAT3 glm::mat3
#define MAT4 glm::mat4

#define UBO_BEGIN(TYPENAME, SET, BINDING) struct TYPENAME {
#define UBO_END(NAME) };
#else
#define VEC2 vec2
#define VEC3 vec3
#define VEC4 vec4
#define MAT2 mat2
#define MAT3 mat3
#define MAT4 mat4

#define UBO_BEGIN(TYPENAME, SET, BINDING) layout(set = SET, binding = BINDING) uniform TYPENAME {
#define UBO_END(NAME) } NAME;
#endif

UBO_BEGIN(UniformBufferObject, 0, 0)
  MAT4 model;
  MAT4 view;
  MAT4 proj;
UBO_END(kUbo)

#endif