#version 330 core
uniform sampler1D data;
uniform float dB_min;
uniform float dB_max;

in vec2 frag_tc0;

out vec4 colour;

void main(void)
{
    vec4 data = texture1D(data, frag_tc0.x);
    float v = max(data.x, data.y);
    v = 20.f * log2(v) / log2(10.f) - dB_min;
    v /= (dB_max - dB_min);

    if (v < frag_tc0.y) {
        discard;
    }

    float shade = max(0.f, v - frag_tc0.y);
    shade = 1 - shade;
    shade = shade*shade;

    colour = vec4(vec3(shade), 1);
}
