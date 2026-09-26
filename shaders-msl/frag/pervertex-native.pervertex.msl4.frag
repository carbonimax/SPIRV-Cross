#version 450
#extension GL_EXT_fragment_shader_barycentric : require

layout(location = 0) pervertexEXT in vec3 color[3];
layout(location = 1) in vec2 uv;
layout(location = 0) out vec4 outputColor;

vec3 read_color(int index)
{
    return color[index];
}

void main()
{
    int index = int(gl_FragCoord.x) % 3;
    outputColor = vec4(read_color(index) + color[0] * 0.2 + color[1] * 0.3 + color[2] * 0.5, uv.x);
}
