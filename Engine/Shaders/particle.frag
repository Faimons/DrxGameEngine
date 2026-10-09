#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D particleTexture;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;
layout(location = 2) in vec3 v_WorldPos;

layout(location = 0) out vec4 o_Color;

void main() {
    vec4 color = texture(particleTexture, v_TexCoord) * v_Color;
    float distance = length(v_WorldPos - frame.cameraPosition.xyz);
    float fog = clamp(1.0 - exp(-frame.fogColor.w * distance), 0.0, 1.0);
    color.rgb = mix(color.rgb, frame.fogColor.rgb, fog * 0.8);
    color.a *= 1.0 - fog * 0.5;
    o_Color = color;
}
