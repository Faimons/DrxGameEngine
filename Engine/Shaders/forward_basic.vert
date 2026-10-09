#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Color;
layout(location = 3) in vec2 a_TexCoord;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 cameraPosition;
    float time;
} frame;

layout(location = 0) out vec3 v_Color;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main() {
    vec4 worldPos = frame.model * vec4(a_Position, 1.0);
    gl_Position = frame.projection * frame.view * worldPos;

    v_Color = a_Color;
    v_Normal = normalize(mat3(frame.model) * a_Normal);
    v_TexCoord = a_TexCoord;
}
