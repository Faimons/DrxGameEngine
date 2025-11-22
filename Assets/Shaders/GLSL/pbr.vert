/**
 * DRX Engine - PBR Vertex Shader
 * Standard vertex shader for PBR materials
 */
#version 450

// Vertex attributes
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec3 a_Tangent;
layout(location = 3) in vec2 a_TexCoord;

// Instance attributes (optional)
layout(location = 4) in mat4 a_InstanceModel;

// Outputs to fragment shader
layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;
layout(location = 3) out mat3 v_TBN;

// Camera uniforms (set 0)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 cameraDirection;
} camera;

// Model matrix (push constant)
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
} push;

void main() {
    // Use instance model if available, otherwise use push constant
    mat4 modelMatrix = push.model;
    mat3 normalMat = mat3(push.normalMatrix);

    // Transform position to world space
    vec4 worldPos = modelMatrix * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;

    // Transform normal to world space
    v_Normal = normalize(normalMat * a_Normal);

    // Calculate TBN matrix for normal mapping
    vec3 T = normalize(normalMat * a_Tangent);
    vec3 N = v_Normal;
    // Re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    v_TBN = mat3(T, B, N);

    // Pass through texture coordinates
    v_TexCoord = a_TexCoord;

    // Final clip space position
    gl_Position = camera.viewProjection * worldPos;
}
