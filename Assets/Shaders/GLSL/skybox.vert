/**
 * DRX Engine - Skybox Vertex Shader
 */
#version 450

// Cube vertices
layout(location = 0) in vec3 a_Position;

// Output to fragment
layout(location = 0) out vec3 v_TexCoord;

// Camera uniforms
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
} camera;

void main() {
    v_TexCoord = a_Position;

    // Remove translation from view matrix
    mat4 viewNoTranslation = mat4(mat3(camera.view));
    vec4 pos = camera.projection * viewNoTranslation * vec4(a_Position, 1.0);

    // Set z to w so depth is always 1.0 (far plane)
    gl_Position = pos.xyww;
}
