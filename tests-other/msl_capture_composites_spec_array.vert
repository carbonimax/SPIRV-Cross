#version 450
layout(constant_id = 0) const int N = 2;
layout(location = 0, component = 2) out vec2 hi[N][2];
layout(location = 0, component = 0) out vec2 lo[N][2];
void main()
{
    for (int i = 0; i < N; i++)
        for (int j = 0; j < 2; j++)
        {
            hi[i][j] = vec2(i, j);
            lo[i][j] = vec2(j, i);
        }
    gl_Position = vec4(0, 0, 0, 1);
}
