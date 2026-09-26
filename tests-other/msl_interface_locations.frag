#version 450

struct Inner { vec3 color; float weights[2]; };
in Located
{
    layout(location = 4) Inner nested;
    layout(location = 10) mat2 basis;
    layout(location = 15) float tail;
    layout(location = 18, component = 2) vec2 upper[2];
    layout(location = 18, component = 0) vec2 lower[2];
    layout(location = 2, component = 1) float early;
    layout(location = 23) Inner structured[2];
} located;
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(located.nested.color, located.tail + located.early);
    color += vec4(located.nested.weights[0], located.nested.weights[1], located.basis[0]);
    color += vec4(located.basis[1], located.upper[0] + located.upper[1] + located.lower[0] + located.lower[1]);
    for (int i = 0; i < 2; i++)
        color += vec4(located.structured[i].color, located.structured[i].weights[0] + located.structured[i].weights[1]);
}
