/**
 * DRX Engine - PBR Fragment Shader
 * Physically Based Rendering with IBL support
 */
#version 450

// Inputs from vertex shader
layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;
layout(location = 3) in mat3 v_TBN;

// Output color
layout(location = 0) out vec4 FragColor;

// Camera uniforms (set 0)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 cameraDirection;
} camera;

// Material uniforms (set 1)
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

// Material textures (set 1)
layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 4) uniform sampler2D aoMap;
layout(set = 1, binding = 5) uniform sampler2D emissiveMap;

// Light structure
struct Light {
    vec4 position;      // xyz: position, w: type (0=dir, 1=point, 2=spot)
    vec4 direction;     // xyz: direction, w: inner cone
    vec4 color;         // xyz: color, w: intensity
    vec4 params;        // x: range, y: outer cone, z: shadow index, w: enabled
};

// Lighting uniforms (set 2)
layout(set = 2, binding = 0) uniform LightingUBO {
    vec4 ambientColor;
    int numLights;
    int padding[3];
} lighting;

layout(set = 2, binding = 1) readonly buffer LightBuffer {
    Light lights[];
};

// IBL textures (set 2)
layout(set = 2, binding = 2) uniform samplerCube irradianceMap;
layout(set = 2, binding = 3) uniform samplerCube prefilterMap;
layout(set = 2, binding = 4) uniform sampler2D brdfLUT;

// Constants
const float PI = 3.14159265359;
const float EPSILON = 0.0001;

// PBR Functions

// Normal Distribution Function (Trowbridge-Reitz GGX)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, EPSILON);
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / max(denom, EPSILON);
}

// Geometry Function (Smith)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel Function (Schlick approximation)
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel with roughness for IBL
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Calculate light attenuation
float CalculateAttenuation(float distance, float range) {
    float attenuation = 1.0 / (distance * distance);
    // Smooth falloff at range boundary
    float factor = distance / range;
    float falloff = clamp(1.0 - factor * factor * factor * factor, 0.0, 1.0);
    return attenuation * falloff * falloff;
}

// Calculate spotlight intensity
float CalculateSpotlight(vec3 L, vec3 spotDir, float innerCone, float outerCone) {
    float theta = dot(L, normalize(-spotDir));
    float epsilon = innerCone - outerCone;
    return clamp((theta - outerCone) / epsilon, 0.0, 1.0);
}

void main() {
    // Sample textures
    vec4 albedo = material.baseColor;
    if (material.useAlbedoMap > 0) {
        albedo *= texture(albedoMap, v_TexCoord);
    }

    // Alpha cutoff for transparency
    if (albedo.a < material.alphaCutoff) {
        discard;
    }

    // Get normal from normal map
    vec3 N = v_Normal;
    if (material.useNormalMap > 0) {
        vec3 normalSample = texture(normalMap, v_TexCoord).rgb;
        normalSample = normalSample * 2.0 - 1.0;
        normalSample.xy *= material.normalStrength;
        N = normalize(v_TBN * normalSample);
    }

    // Sample metallic-roughness
    float metallic = material.metallic;
    float roughness = material.roughness;
    if (material.useMetallicRoughnessMap > 0) {
        vec4 mr = texture(metallicRoughnessMap, v_TexCoord);
        metallic *= mr.b;      // Blue channel for metallic
        roughness *= mr.g;     // Green channel for roughness
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // Sample AO
    float ao = material.ao * texture(aoMap, v_TexCoord).r;

    // Sample emissive
    vec3 emissive = material.emissiveColor * material.emissiveStrength;
    emissive *= texture(emissiveMap, v_TexCoord).rgb;

    // Calculate view direction
    vec3 V = normalize(camera.cameraPosition.xyz - v_WorldPos);
    vec3 R = reflect(-V, N);

    // Calculate reflectance at normal incidence (F0)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);

    // Direct lighting
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < lighting.numLights; i++) {
        Light light = lights[i];
        if (light.params.w < 0.5) continue;  // Light disabled

        vec3 L;
        float attenuation = 1.0;

        int lightType = int(light.position.w);

        if (lightType == 0) {
            // Directional light
            L = normalize(-light.direction.xyz);
        }
        else if (lightType == 1) {
            // Point light
            vec3 lightVec = light.position.xyz - v_WorldPos;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = CalculateAttenuation(distance, light.params.x);
        }
        else {
            // Spot light
            vec3 lightVec = light.position.xyz - v_WorldPos;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = CalculateAttenuation(distance, light.params.x);
            attenuation *= CalculateSpotlight(L, light.direction.xyz,
                                              light.direction.w, light.params.y);
        }

        vec3 H = normalize(V + L);
        vec3 radiance = light.color.rgb * light.color.w * attenuation;

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON;
        vec3 specular = numerator / denominator;

        // Energy conservation
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;  // Metallic surfaces don't have diffuse

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL;
    }

    // Image-Based Lighting (IBL)
    vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;

    // Diffuse IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo.rgb;

    // Specular IBL
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular) * ao;
    ambient += lighting.ambientColor.rgb * albedo.rgb * ao;

    // Final color
    vec3 color = ambient + Lo + emissive;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, albedo.a);
}
