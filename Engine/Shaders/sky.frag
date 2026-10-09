#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec2 v_NDC;
layout(location = 0) out vec4 o_Color;

void main() {
    vec4 farPoint = frame.inverseViewProjection * vec4(v_NDC, 1.0, 1.0);
    vec3 direction = normalize(farPoint.xyz / farPoint.w - frame.cameraPosition.xyz);

    float height = direction.y;
    vec3 sky = mix(frame.skyHorizon.rgb, frame.skyZenith.rgb, pow(clamp(height, 0.0, 1.0), 0.45));
    vec3 ground = mix(frame.skyHorizon.rgb, frame.groundColor.rgb, clamp(-height * 4.0, 0.0, 1.0));
    vec3 color = height >= 0.0 ? sky : ground;

    // Sun disc and glow
    vec3 sunDir = normalize(frame.sunDirection.xyz);
    float sunAmount = max(dot(direction, sunDir), 0.0);
    vec3 sunTint = frame.sunColor.rgb;
    color += sunTint * pow(sunAmount, 900.0) * 20.0;
    color += sunTint * pow(sunAmount, 12.0) * 0.25;

    // Fog blends the horizon into the scene fog colour
    float horizonFog = clamp(1.0 - abs(height) * 6.0, 0.0, 1.0) * clamp(frame.fogColor.w * 60.0, 0.0, 1.0);
    color = mix(color, frame.fogColor.rgb, horizonFog);

    o_Color = vec4(ToneMapACES(color * frame.exposure.x), 1.0);
}
