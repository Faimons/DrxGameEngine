// Shared declarations for DRX scene shaders (std140 layout must match FrameUniforms in C++)

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseViewProjection;
    mat4 lightViewProjection;
    vec4 cameraPosition;     // xyz, w = time in seconds
    vec4 sunDirection;       // xyz towards the sun, w = intensity
    vec4 sunColor;           // rgb, w = shadow strength
    vec4 skyZenith;          // rgb
    vec4 skyHorizon;         // rgb
    vec4 groundColor;        // rgb
    vec4 ambientSky;         // rgb, w = ambient intensity
    vec4 ambientGround;      // rgb
    vec4 fogColor;           // rgb, w = density
    vec4 shadowParams;       // x = texel size, y = depth bias, z = enabled, w = unused
    vec4 exposure;           // x = exposure, yzw unused
} frame;

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

layout(push_constant) uniform DrawConstants {
    mat4 model;
    vec4 color;              // rgba tint
    vec4 params;             // x = roughness, y = metallic, z = emissive strength, w = flags
    vec4 emissive;           // rgb emissive color
} draw;

vec3 ToneMapACES(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 ApplyFog(vec3 color, vec3 worldPos) {
    float distance = length(worldPos - frame.cameraPosition.xyz);
    float fog = 1.0 - exp(-frame.fogColor.w * distance);
    return mix(color, frame.fogColor.rgb, clamp(fog, 0.0, 1.0));
}
