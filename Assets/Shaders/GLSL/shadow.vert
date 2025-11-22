/**
 * DRX Engine - Shadow Map Vertex Shader
 * Renders depth from light's perspective
 */
#version 450

// Vertex attributes
layout(location = 0) in vec3 a_Position;

// Light space matrix (push constant)
layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
    mat4 model;
} push;

void main() {
    gl_Position = push.lightSpaceMatrix * push.model * vec4(a_Position, 1.0);
}
