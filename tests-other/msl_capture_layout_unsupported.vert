#version 450
#extension GL_ARB_gpu_shader_int64 : require
layout(location = 0) flat out uint64_t wide;
layout(location = 1) out mat2 matrixOutput;
void main()
{
    wide = uint64_t(1);
    matrixOutput = mat2(1);
    gl_Position = vec4(0, 0, 0, 1);
}
