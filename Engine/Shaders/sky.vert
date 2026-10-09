#version 450

layout(location = 0) out vec2 v_NDC;

void main() {
    // Fullscreen triangle
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    v_NDC = positions[gl_VertexIndex];
    gl_Position = vec4(v_NDC, 1.0, 1.0);
}
