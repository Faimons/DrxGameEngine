#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Color;
layout(location = 3) in vec2 a_TexCoord;

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec3 v_Color;
layout(location = 3) out vec2 v_TexCoord;
layout(location = 4) out vec4 v_LightSpacePos;

void main() {
    vec4 worldPos = draw.model * vec4(a_Position, 1.0);
    gl_Position = frame.viewProjection * worldPos;

    // Normal matrix from the model matrix (handles non-uniform scale)
    mat3 normalMatrix = transpose(inverse(mat3(draw.model)));

    v_WorldPos = worldPos.xyz;
    v_Normal = normalize(normalMatrix * a_Normal);
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_LightSpacePos = frame.lightViewProjection * worldPos;
}
