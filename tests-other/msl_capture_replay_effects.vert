#version 450
layout(set = 0, binding = 0, std430) buffer AppSideEffects { uint counter; } appSideEffects;
layout(location = 0) noperspective out vec3 color;
layout(location = 0, component = 3) noperspective out float alpha;
layout(location = 2) flat out uint spvReplayPrimitive;
void app_write_side_effect()
{
    atomicAdd(appSideEffects.counter, 1u);
}
void main()
{
    app_write_side_effect();
    color = vec3(gl_VertexIndex, gl_InstanceIndex, 1);
    alpha = 0.5;
    spvReplayPrimitive = 7u;
    gl_Position = vec4(color, 1);
}
