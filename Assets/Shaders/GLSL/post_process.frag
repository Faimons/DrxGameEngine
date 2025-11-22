/**
 * DRX Engine - Post Process Fragment Shader
 * Applies various post-processing effects
 */
#version 450

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 FragColor;

// Input texture
layout(set = 0, binding = 0) uniform sampler2D screenTexture;
layout(set = 0, binding = 1) uniform sampler2D bloomTexture;
layout(set = 0, binding = 2) uniform sampler2D depthTexture;

// Post-process settings
layout(set = 0, binding = 3) uniform PostProcessUBO {
    float exposure;
    float gamma;
    float bloomIntensity;
    float vignetteIntensity;
    float vignetteRadius;
    float chromaticAberration;
    float filmGrain;
    float time;
    vec4 colorGrading;  // RGB multiplier + saturation
    int tonemapOperator; // 0=None, 1=Reinhard, 2=ACES, 3=Uncharted2
    int enableBloom;
    int enableVignette;
    int enableFXAA;
} settings;

// ACES Filmic Tone Mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Uncharted 2 Tone Mapping
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

// Film grain
float rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

// FXAA (simplified)
vec3 FXAA(sampler2D tex, vec2 uv, vec2 texelSize) {
    float FXAA_SPAN_MAX = 8.0;
    float FXAA_REDUCE_MUL = 1.0 / 8.0;
    float FXAA_REDUCE_MIN = 1.0 / 128.0;

    vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * texelSize).rgb;
    vec3 rgbNE = texture(tex, uv + vec2( 1.0, -1.0) * texelSize).rgb;
    vec3 rgbSW = texture(tex, uv + vec2(-1.0,  1.0) * texelSize).rgb;
    vec3 rgbSE = texture(tex, uv + vec2( 1.0,  1.0) * texelSize).rgb;
    vec3 rgbM  = texture(tex, uv).rgb;

    vec3 luma = vec3(0.299, 0.587, 0.114);
    float lumaNW = dot(rgbNW, luma);
    float lumaNE = dot(rgbNE, luma);
    float lumaSW = dot(rgbSW, luma);
    float lumaSE = dot(rgbSE, luma);
    float lumaM  = dot(rgbM,  luma);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * FXAA_REDUCE_MUL), FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = min(vec2(FXAA_SPAN_MAX), max(vec2(-FXAA_SPAN_MAX), dir * rcpDirMin)) * texelSize;

    vec3 rgbA = 0.5 * (
        texture(tex, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(tex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(tex, uv + dir * -0.5).rgb +
        texture(tex, uv + dir *  0.5).rgb);

    float lumaB = dot(rgbB, luma);

    if ((lumaB < lumaMin) || (lumaB > lumaMax)) {
        return rgbA;
    }
    return rgbB;
}

void main() {
    vec2 texelSize = 1.0 / textureSize(screenTexture, 0);

    // Sample scene color (with optional FXAA)
    vec3 color;
    if (settings.enableFXAA > 0) {
        color = FXAA(screenTexture, v_TexCoord, texelSize);
    } else {
        color = texture(screenTexture, v_TexCoord).rgb;
    }

    // Chromatic aberration
    if (settings.chromaticAberration > 0.0) {
        vec2 offset = (v_TexCoord - 0.5) * settings.chromaticAberration * 0.01;
        color.r = texture(screenTexture, v_TexCoord + offset).r;
        color.b = texture(screenTexture, v_TexCoord - offset).b;
    }

    // Add bloom
    if (settings.enableBloom > 0) {
        vec3 bloom = texture(bloomTexture, v_TexCoord).rgb;
        color += bloom * settings.bloomIntensity;
    }

    // Apply exposure
    color *= settings.exposure;

    // Tone mapping
    if (settings.tonemapOperator == 1) {
        // Reinhard
        color = color / (color + vec3(1.0));
    } else if (settings.tonemapOperator == 2) {
        // ACES
        color = ACESFilm(color);
    } else if (settings.tonemapOperator == 3) {
        // Uncharted 2
        float W = 11.2;
        color = Uncharted2Tonemap(color * 2.0);
        vec3 whiteScale = 1.0 / Uncharted2Tonemap(vec3(W));
        color *= whiteScale;
    }

    // Color grading
    color *= settings.colorGrading.rgb;

    // Saturation
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luminance), color, settings.colorGrading.a);

    // Vignette
    if (settings.enableVignette > 0) {
        vec2 uv = v_TexCoord * (1.0 - v_TexCoord);
        float vignette = uv.x * uv.y * 15.0;
        vignette = pow(vignette, settings.vignetteIntensity * settings.vignetteRadius);
        color *= vignette;
    }

    // Film grain
    if (settings.filmGrain > 0.0) {
        float grain = rand(v_TexCoord + vec2(settings.time)) * 2.0 - 1.0;
        color += grain * settings.filmGrain * 0.1;
    }

    // Gamma correction
    color = pow(color, vec3(1.0 / settings.gamma));

    FragColor = vec4(color, 1.0);
}
