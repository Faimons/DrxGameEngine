#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec3 v_Color;
layout(location = 3) in vec2 v_TexCoord;
layout(location = 4) in vec4 v_LightSpacePos;

layout(location = 0) out vec4 o_Color;

float SampleShadow(vec3 normal) {
    if (frame.shadowParams.z < 0.5) {
        return 1.0;
    }

    vec3 projected = v_LightSpacePos.xyz / v_LightSpacePos.w;
    vec2 uv = projected.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || projected.z > 1.0) {
        return 1.0;
    }

    // Slope-scaled bias against acne on surfaces facing away from the sun
    float ndotl = clamp(dot(normal, frame.sunDirection.xyz), 0.0, 1.0);
    float bias = frame.shadowParams.y * (1.0 + 2.0 * (1.0 - ndotl));
    float depth = projected.z - bias;

    // 3x3 PCF
    float texel = frame.shadowParams.x;
    float lit = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            lit += texture(shadowMap, vec3(uv + vec2(x, y) * texel, depth));
        }
    }
    lit /= 9.0;

    return mix(1.0, lit, frame.sunColor.w);
}

void main() {
    vec4 texel = texture(albedoTexture, v_TexCoord);
    vec3 albedo = texel.rgb * v_Color * draw.color.rgb;
    float alpha = texel.a * draw.color.a;
    if (alpha < 0.05) {
        discard;
    }

    vec3 N = normalize(v_Normal);
    if (!gl_FrontFacing) {
        N = -N;  // double-sided materials
    }
    vec3 V = normalize(frame.cameraPosition.xyz - v_WorldPos);
    vec3 L = normalize(frame.sunDirection.xyz);
    vec3 H = normalize(L + V);

    float roughness = clamp(draw.params.x, 0.04, 1.0);
    float metallic = clamp(draw.params.y, 0.0, 1.0);

    float ndotl = max(dot(N, L), 0.0);
    float shininess = mix(256.0, 4.0, roughness);
    float specularTerm = pow(max(dot(N, H), 0.0), shininess) * (shininess + 8.0) / 25.13274;
    vec3 specularColor = mix(vec3(0.04), albedo, metallic);
    vec3 diffuseColor = albedo * (1.0 - metallic);

    float shadow = SampleShadow(N);
    vec3 sun = frame.sunColor.rgb * frame.sunDirection.w;
    vec3 direct = (diffuseColor + specularColor * specularTerm * (1.0 - roughness * 0.7)) * sun * ndotl * shadow;

    float hemisphere = N.y * 0.5 + 0.5;
    vec3 ambient = mix(frame.ambientGround.rgb, frame.ambientSky.rgb, hemisphere) * frame.ambientSky.w;
    vec3 indirect = ambient * (diffuseColor + specularColor * 0.25);

    vec3 color = direct + indirect + draw.emissive.rgb * draw.params.z;
    color = ApplyFog(color, v_WorldPos);
    color = ToneMapACES(color * frame.exposure.x);

    o_Color = vec4(color, alpha);
}
