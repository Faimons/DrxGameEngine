/**
 * DRX Engine - Post Process Vertex Shader
 * Full-screen quad for post-processing effects
 */
#version 450

// Full-screen triangle
vec2 positions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

vec2 texCoords[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0)
);

layout(location = 0) out vec2 v_TexCoord;

void main() {
    v_TexCoord = texCoords[gl_VertexIndex];
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
