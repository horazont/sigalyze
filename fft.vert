#version 330 core
in vec2 position;
in vec2 tc0;

out vec2 frag_tc0;

void main(void)
{
    frag_tc0 = tc0;
    gl_Position = vec4(position, 0.f, 1.f);
}
