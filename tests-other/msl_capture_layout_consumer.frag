#version 450
#extension GL_EXT_fragment_shader_barycentric : require
layout(location = 0) pervertexEXT in vec3 color[3];
layout(location = 0) out vec4 outputColor;
void main()
{
    outputColor = vec4(color[0] * 0.2 + color[1] * 0.3 + color[2] * 0.5, 1);
}
