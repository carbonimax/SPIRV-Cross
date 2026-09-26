#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
struct Leaf { f16vec3 h; int16_t s[2]; uint16_t u; ivec2 i; uvec3 j; f16mat2x3 m[2]; };
layout(location=0) out Leaf typed[2];
void main() { for (int n=0;n<2;n++) typed[n]=Leaf(f16vec3(1),int16_t[2](int16_t(-2),int16_t(3)),uint16_t(4),ivec2(-5),uvec3(6),f16mat2x3[2](f16mat2x3(1),f16mat2x3(2))); gl_Position=vec4(0,0,0,1); }
