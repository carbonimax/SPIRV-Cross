#version 450
#extension GL_EXT_fragment_shader_barycentric : require
layout(location = 0) pervertexEXT in vec3 vertices[3];
layout(location = 0) out vec3 color;
vec3 shade()
{
    return vertices[0] * gl_BaryCoordEXT.x + vertices[1] * gl_BaryCoordEXT.y + vertices[2] * gl_BaryCoordEXT.z;
}
void main()
{
    color = shade() + vertices[0] * gl_BaryCoordNoPerspEXT.x + vertices[1] * gl_BaryCoordNoPerspEXT.y + vertices[2] * gl_BaryCoordNoPerspEXT.z;
}
