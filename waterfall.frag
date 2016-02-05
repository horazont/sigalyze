#version 330 core
#extension GL_ARB_texture_gather: require

uniform sampler2DArray data;
uniform int layer;
uniform float dB_min;
uniform float dB_max;

in vec2 frag_tc0;

out vec4 colour;

const float M_PI = 3.14159;

vec3 cubehelix(float v, float s, float r, float h)
{
    float a = h*v*(1.f-v)/2.f;
    float phi = 2.*M_PI*(s/3. + r*v);

    float cos_phi = cos(phi);
    float sin_phi = sin(phi);

    float rf = clamp(v + a*(-0.14861*cos_phi + 1.78277*sin_phi), 0, 1);
    float gf = clamp(v + a*(-0.29227*cos_phi - 0.90649*sin_phi), 0, 1);
    float bf = clamp(v + a*(1.97294*cos_phi), 0, 1);

    return vec3(rf, gf, bf);
}

void main()
{
    vec4 data = textureGather(data, vec3(frag_tc0, layer));
    float v = max(data.x, data.y);
    v = 20.f * log2(v) / log2(10.f) - dB_min;
    v /= (dB_max - dB_min);

    // allow for 10% of the range to be below 0
    v += 0.1;

    colour = vec4(cubehelix(v, M_PI/12., -1.0, 1.0), 1.f);
}
