/**
 * DRX Engine - Deferred Lighting Fragment Shader
 * Reads G-Buffer and applies lighting
 */
#version 450

// Input from vertex shader
layout(location = 0) in vec2 v_TexCoord;

// Output color
layout(location = 0) out vec4 FragColor;

// G-Buffer textures
layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gAlbedo;
layout(set = 0, binding = 3) uniform sampler2D gMetallicRoughness;
layout(set = 0, binding = 4) uniform sampler2D gEmissive;
layout(set = 0, binding = 5) uniform sampler2D gDepth;

// Light structure
struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

// Lighting uniforms
layout(set = 1, binding = 0) uniform LightingUBO {
    mat4 invViewProjection;
    vec4 cameraPosition;
    vec4 ambientColor;
    int numLights;
    int padding[3];
} lighting;

layout(set = 1, binding = 1) readonly buffer LightBuffer {
    Light lights[];
};

// IBL textures
layout(set = 1, binding = 2) uniform samplerCube irradianceMap;
layout(set = 1, binding = 3) uniform samplerCube prefilterMap;
layout(set = 1, binding = 4) uniform sampler2D brdfLUT;

// Constants
const float PI = 3.14159265359;
const float EPSILON = 0.0001;

// PBR Functions (same as forward shader)
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

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float CalculateAttenuation(float distance, float range) {
    float attenuation = 1.0 / (distance * distance);
    float factor = distance / range;
    float smooth = clamp(1.0 - factor * factor * factor * factor, 0.0, 1.0);
    return attenuation * smooth * smooth;
}

void main() {
    // Sample G-Buffer
    vec3 worldPos = texture(gPosition, v_TexCoord).xyz;
    vec3 N = texture(gNormal, v_TexCoord).xyz * 2.0 - 1.0;  // Decode normal
    vec4 albedoAlpha = texture(gAlbedo, v_TexCoord);
    vec4 metallicRoughnessAO = texture(gMetallicRoughness, v_TexCoord);
    vec4 emissiveData = texture(gEmissive, v_TexCoord);
    float depth = texture(gDepth, v_TexCoord).r;

    // Early out for skybox
    if (depth >= 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 albedo = albedoAlpha.rgb;
    float metallic = metallicRoughnessAO.r;
    float roughness = max(metallicRoughnessAO.g, 0.04);
    float ao = metallicRoughnessAO.b;
    vec3 emissive = emissiveData.rgb * emissiveData.a;

    // View direction
    vec3 V = normalize(lighting.cameraPosition.xyz - worldPos);
    vec3 R = reflect(-V, N);

    // F0 for Fresnel
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Direct lighting
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < lighting.numLights; i++) {
        Light light = lights[i];
        if (light.params.w < 0.5) continue;

        vec3 L;
        float attenuation = 1.0;

        int lightType = int(light.position.w);

        if (lightType == 0) {
            // Directional
            L = normalize(-light.direction.xyz);
        } else if (lightType == 1) {
            // Point
            vec3 lightVec = light.position.xyz - worldPos;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = CalculateAttenuation(distance, light.params.x);
        } else {
            // Spot
            vec3 lightVec = light.position.xyz - worldPos;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = CalculateAttenuation(distance, light.params.x);

            float theta = dot(L, normalize(-light.direction.xyz));
            float epsilon = light.direction.w - light.params.y;
            attenuation *= clamp((theta - light.params.y) / epsilon, 0.0, 1.0);
        }

        vec3 H = normalize(V + L);
        vec3 radiance = light.color.rgb * light.color.w * attenuation;

        // BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + EPSILON;
        vec3 specular = numerator / denominator;

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float NdotL = max(dot(N, L), 0.0);

        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // IBL
    vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metallic);

    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular) * ao;
    ambient += lighting.ambientColor.rgb * albedo * ao;

    // Final color
    vec3 color = ambient + Lo + emissive;

    // Tone mapping
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    FragColor = vec4(color, 1.0);
}
