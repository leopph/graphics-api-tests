#ifndef SHADER_INTEROP_H
#define SHADER_INTEROP_H

#ifdef __cplusplus
#include <array>
#define FLOAT4 std::array<float, 4>
#define CBUFFER_BEGIN(name, slot) struct name {
#define CBUFFER_END };
#else
#define FLOAT4 float4
#define CONCAT(x, y) x ## y
#define CBUFFER_BEGIN1(name, reg) cbuffer name : register(reg) {
#define CBUFFER_BEGIN(name, slot) CBUFFER_BEGIN1(name, b ## slot)
#define CBUFFER_END }
#endif

#define CONSTANT_BUFFER_SLOT 0

CBUFFER_BEGIN(ConstantBuffer, CONSTANT_BUFFER_SLOT)
  FLOAT4 color;
CBUFFER_END

#endif