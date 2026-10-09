#version 450

layout(push_constant) uniform UIConstants {
    vec4 transform;
    vec4 mode;
} ui;

layout(set = 1, binding = 0) uniform sampler2D uiTexture;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) in vec4 v_Color;

layout(location = 0) out vec4 o_Color;

void main() {
    vec4 texel = texture(uiTexture, v_TexCoord);
    if (ui.mode.x < 0.5) {
        o_Color = vec4(v_Color.rgb, v_Color.a * texel.r);
    } else {
        o_Color = texel * v_Color;
    }
}
