/**
 * DRX Engine - G-Buffer Vertex Shader
 * Outputs geometry data to multiple render targets
 */
#version 450

// Vertex attributes
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec2 a_TexCoord;

// Outputs to fragment shader
layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out mat3 v_TBN;

// Camera uniforms
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
} camera;

// Model matrix (push constant)
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
} push;

void main() {
    mat4 modelMatrix = push.model;
    mat3 normalMat = mat3(push.normalMatrix);

    // World position
    vec4 worldPos = modelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;

    // Normal in world space
    v_Normal = normalize(normalMat * a_Normal);

    // TBN matrix
    vec3 T = normalize(normalMat * a_Tangent);
    vec3 N = v_Normal;
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    v_TBN = mat3(T, B, N);

    // Texture coordinates
    v_TexCoord = a_TexCoord;

    // Clip space position
    gl_Position = camera.viewProjection * worldPos;
}
