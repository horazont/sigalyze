#version 330 core

uniform mat4 proj;
uniform float offset;

in vec2 position;
in vec2 tc0;

out vec2 frag_tc0;

void main(void)
{
    gl_Position = proj * vec4(position + vec2(0, offset), 0, 1);
    frag_tc0 = tc0;
}
