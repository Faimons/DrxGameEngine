#version 450

layout(location = 0) in vec3 v_Color;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Color;

void main() {
    // Simple two-sided lambert so the spinning demo geometry stays readable
    vec3 lightDir = normalize(vec3(0.4, 0.8, 0.6));
    float diffuse = abs(dot(normalize(v_Normal), lightDir));
    o_Color = vec4(v_Color * (0.35 + 0.65 * diffuse), 1.0);
}
