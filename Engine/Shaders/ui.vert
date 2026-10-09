#version 450

layout(push_constant) uniform UIConstants {
    vec4 transform;   // xy = scale, zw = translate (pixels -> NDC)
    vec4 mode;        // x = 0: alpha from red channel (font/solid), 1: rgba texture
} ui;

layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_TexCoord;
layout(location = 2) in vec4 a_Color;

layout(location = 0) out vec2 v_TexCoord;
layout(location = 1) out vec4 v_Color;

void main() {
    gl_Position = vec4(a_Position * ui.transform.xy + ui.transform.zw, 0.0, 1.0);
    v_TexCoord = a_TexCoord;
    v_Color = a_Color;
}
