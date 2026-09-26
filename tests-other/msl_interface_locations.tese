#version 450
layout(triangles, equal_spacing, cw) in;

in B
{
    layout(location = 0, component = 0) vec2 lo[2];
    layout(location = 0, component = 2) vec2 hi[2];
} b[];

void main()
{
    gl_Position = vec4(b[0].lo[0] + b[0].lo[1], b[0].hi[0] + b[0].hi[1]);
}
