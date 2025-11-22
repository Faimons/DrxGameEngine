/**
 * DRX Engine - Skybox Fragment Shader
 */
#version 450

// Input from vertex shader
layout(location = 0) in vec3 v_TexCoord;

// Output color
layout(location = 0) out vec4 FragColor;

// Skybox cubemap
layout(set = 0, binding = 1) uniform samplerCube skybox;

void main() {
    vec3 color = texture(skybox, v_TexCoord).rgb;

    // HDR tone mapping
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
