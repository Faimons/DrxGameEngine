/**
 * DRX Engine - G-Buffer Fragment Shader
 * Writes to multiple render targets for deferred shading
 */
#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) in mat3 v_TBN;

// G-Buffer outputs
layout(location = 0) out vec4 gPosition;         // World position
layout(location = 1) out vec4 gNormal;           // World normal
layout(location = 2) out vec4 gAlbedo;           // Albedo + alpha
layout(location = 3) out vec4 gMetallicRoughness; // R: metallic, G: roughness, B: AO
layout(location = 4) out vec4 gEmissive;         // Emissive color + strength

// Material uniforms
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float normalStrength;
    vec3 emissiveColor;
    float emissiveStrength;
    float alphaCutoff;
    int useAlbedoMap;
    int useNormalMap;
    int useMetallicRoughnessMap;
} material;

// Material textures
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

void main() {
    // Sample albedo
    vec4 albedo = material.baseColor;
    if (material.useAlbedoMap > 0) {
        albedo *= texture(albedoMap, v_TexCoord);
    }

    // Alpha cutoff
    if (albedo.a < material.alphaCutoff) {
        discard;
    }

    // Get normal
    vec3 N = v_Normal;
    if (material.useNormalMap > 0) {
        vec3 normalSample = texture(normalMap, v_TexCoord).rgb * 2.0 - 1.0;
        normalSample.xy *= material.normalStrength;
        N = normalize(v_TBN * normalSample);
    }

    // Sample metallic-roughness
    float metallic = material.metallic;
    float roughness = material.roughness;
    if (material.useMetallicRoughnessMap > 0) {
        vec4 mr = texture(metallicRoughnessMap, v_TexCoord);
        metallic *= mr.b;
        roughness *= mr.g;
    }

    // Sample AO
    float ao = material.ao * texture(aoMap, v_TexCoord).r;

    // Sample emissive
    vec3 emissive = material.emissiveColor * texture(emissiveMap, v_TexCoord).rgb;

    // Write to G-Buffer
    gPosition = vec4(v_WorldPos, 1.0);
    gNormal = vec4(N * 0.5 + 0.5, 1.0);  // Encode normal to [0,1]
    gAlbedo = albedo;
    gMetallicRoughness = vec4(metallic, roughness, ao, 1.0);
    gEmissive = vec4(emissive, material.emissiveStrength);
}
