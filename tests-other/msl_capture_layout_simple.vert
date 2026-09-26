#version 450
layout(location = 0) out vec3 color;
void main()
{
    color = vec3(1, 2, 3);
    gl_Position = vec4(0, 0, 0, 1);
}
