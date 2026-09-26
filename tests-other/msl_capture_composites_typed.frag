#version 450
#extension GL_EXT_fragment_shader_barycentric : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_16bit_storage : require
struct Leaf { f16vec3 h; int16_t s[2]; uint16_t u; ivec2 i; uvec3 j; f16mat2x3 m[2]; };
layout(location=0) pervertexEXT in Leaf typed[3][2];
layout(location=0) out vec4 color;
void main() { int v=int(gl_FragCoord.x)%3; int n=int(gl_FragCoord.y)%2; color=vec4(vec3(typed[v][n].h)+vec3(typed[v][n].j)+vec3(typed[v][n].m[1][1]),float(typed[v][n].s[1])+float(typed[v][n].u)+float(typed[v][n].i.x)); }
