//------------------------------------------------------------------------------
// PBR.frag - PBR Fragment Shader (Vulkan GLSL)
//------------------------------------------------------------------------------
// Copyright Monster Engine. All Rights Reserved.
// Reference: Google Filament, UE5 BasePassPixelShader
//
// Implements physically based rendering with:
// - GGX specular BRDF (D_GGX, V_SmithGGXCorrelated, F_Schlick)
// - Lambertian diffuse
// - Direct lighting (point, directional, spot lights)
// - Analytic studio environment with diffuse and specular indirect lighting
//
// Row-major matrices, row vector multiplication (UE5 convention)
// Non-reverse Z depth buffer
//------------------------------------------------------------------------------

#version 450

//------------------------------------------------------------------------------
// Include BRDF functions
//------------------------------------------------------------------------------

// Constants
#define PI 3.14159265358979323846
#define INV_PI 0.31830988618379067154
#define EPSILON 1e-6
#define MIN_ROUGHNESS 0.045
#define MIN_N_DOT_V 1e-4

// Maximum number of lights
#define MAX_LIGHTS 8

//------------------------------------------------------------------------------
// Utility Functions
//------------------------------------------------------------------------------

float saturate(float x) {
    return clamp(x, 0.0, 1.0);
}

vec3 saturateVec3(vec3 x) {
    return clamp(x, vec3(0.0), vec3(1.0));
}

float sq(float x) {
    return x * x;
}

float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

//------------------------------------------------------------------------------
// BRDF Functions
//------------------------------------------------------------------------------

// GGX Normal Distribution Function
float D_GGX(float alpha, float NoH) {
    float a2 = alpha * alpha;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(PI * d * d, 1e-10);
}

// Smith GGX Correlated Visibility Function
float V_SmithGGXCorrelated(float alpha, float NoV, float NoL) {
    float a2 = alpha * alpha;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5 / (lambdaV + lambdaL + EPSILON);
}

// Schlick Fresnel Approximation
vec3 F_Schlick(vec3 f0, float VoH) {
    return f0 + (1.0 - f0) * pow5(1.0 - VoH);
}

vec3 F_Schlick(vec3 f0, float f90, float VoH) {
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

// Lambertian Diffuse
float Fd_Lambert() {
    return INV_PI;
}

//------------------------------------------------------------------------------
// Descriptor Set 0: Per-Frame Data
//------------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform ViewUniformBuffer {
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    mat4 InvViewMatrix;
    mat4 InvProjectionMatrix;
    mat4 InvViewProjectionMatrix;
    vec4 CameraPosition;
    vec4 CameraDirection;
    vec4 ViewportSize;
    vec4 TimeParams;
} View;

// Light types
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2

struct LightData {
    vec4 Position;        // xyz = position (point/spot) or direction (directional), w = type
    vec4 Color;           // xyz = color, w = intensity
    vec4 Direction;       // xyz = direction (spot), w = unused
    vec4 Attenuation;     // x = range, y = inner cone, z = outer cone, w = unused
};

layout(set = 0, binding = 1) uniform LightUniformBuffer {
    LightData Lights[MAX_LIGHTS];
    int NumLights;
    float AmbientIntensity;
    vec2 Padding;
} Lighting;

//------------------------------------------------------------------------------
// Descriptor Set 1: Per-Material Data
//------------------------------------------------------------------------------

layout(set = 1, binding = 0) uniform MaterialUniformBuffer {
    vec4 BaseColorFactor;     // xyz = base color, w = alpha
    vec4 EmissiveFactor;      // xyz = emissive color, w = unused
    float MetallicFactor;
    float RoughnessFactor;
    float ReflectanceFactor;  // Dielectric reflectance (default 0.5 = 4%)
    float AmbientOcclusion;
    float AlphaCutoff;
    float ClearCoat;
    float ClearCoatRoughness;
    float Padding;
} Material;

// Material textures
layout(set = 1, binding = 1) uniform sampler2D BaseColorMap;
layout(set = 1, binding = 2) uniform sampler2D NormalMap;
layout(set = 1, binding = 3) uniform sampler2D MetallicRoughnessMap;  // G = roughness, B = metallic
layout(set = 1, binding = 4) uniform sampler2D OcclusionMap;
layout(set = 1, binding = 5) uniform sampler2D EmissiveMap;

//------------------------------------------------------------------------------
// Fragment Inputs
//------------------------------------------------------------------------------

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec4 inWorldTangent;
layout(location = 3) in vec2 inTexCoord0;
layout(location = 4) in vec2 inTexCoord1;
layout(location = 5) in vec4 inVertexColor;
layout(location = 6) in vec3 inViewDirection;

//------------------------------------------------------------------------------
// Fragment Outputs
//------------------------------------------------------------------------------

layout(location = 0) out vec4 outColor;

//------------------------------------------------------------------------------
// Normal Mapping
//------------------------------------------------------------------------------

vec3 getNormalFromMap() {
    // Sample normal map (tangent space normal)
    vec3 tangentNormal = texture(NormalMap, inTexCoord0).xyz * 2.0 - 1.0;
    
    // Build TBN matrix
    vec3 N = normalize(inWorldNormal);
    vec3 T = normalize(inWorldTangent.xyz);
    
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    
    // Compute bitangent
    vec3 B = cross(N, T) * inWorldTangent.w;
    
    // TBN matrix transforms from tangent space to world space
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

//------------------------------------------------------------------------------
// Material Evaluation
//------------------------------------------------------------------------------

struct MaterialParams {
    vec3 baseColor;
    float metallic;
    float roughness;
    float reflectance;
    float ao;
    vec3 emissive;
    vec3 normal;
};

MaterialParams evaluateMaterial() {
    MaterialParams params;
    
    // Color textures use sRGB GPU formats; samples are already linear.
    vec4 baseColorSample = texture(BaseColorMap, inTexCoord0);
    params.baseColor = baseColorSample.rgb * Material.BaseColorFactor.rgb * inVertexColor.rgb;
    
    // Sample metallic-roughness (glTF convention: G = roughness, B = metallic)
    vec4 mrSample = texture(MetallicRoughnessMap, inTexCoord0);
    params.roughness = mrSample.g * Material.RoughnessFactor;
    params.metallic = mrSample.b * Material.MetallicFactor;
    
    // Clamp roughness to avoid numerical issues
    params.roughness = clamp(params.roughness, MIN_ROUGHNESS, 1.0);
    params.metallic = saturate(params.metallic);
    
    // Reflectance for dielectrics
    params.reflectance = Material.ReflectanceFactor;
    
    // Sample ambient occlusion
    params.ao = texture(OcclusionMap, inTexCoord0).r * Material.AmbientOcclusion;
    
    // Sample emissive
    params.emissive = texture(EmissiveMap, inTexCoord0).rgb * Material.EmissiveFactor.rgb;
    
    // Get normal from normal map
    params.normal = getNormalFromMap();
    
    return params;
}

//------------------------------------------------------------------------------
// Lighting Calculation
//------------------------------------------------------------------------------

// Compute F0 (specular reflectance at normal incidence)
vec3 computeF0(vec3 baseColor, float metallic, float reflectance) {
    vec3 dielectricF0 = vec3(0.16 * reflectance * reflectance);
    return mix(dielectricF0, baseColor, metallic);
}

// Compute diffuse color (metals have no diffuse)
vec3 computeDiffuseColor(vec3 baseColor, float metallic) {
    return baseColor * (1.0 - metallic);
}

// Evaluate direct lighting from a single light
vec3 evaluateDirectLight(
    vec3 N,           // Surface normal
    vec3 V,           // View direction
    vec3 L,           // Light direction
    vec3 lightColor,  // Light color * intensity
    vec3 diffuseColor,
    vec3 f0,
    float roughness
) {
    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0) return vec3(0.0);
    
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    
    // Half vector
    vec3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float LoH = saturate(dot(L, H));
    
    // Roughness to alpha
    float alpha = roughness * roughness;
    
    // Specular BRDF
    float D = D_GGX(alpha, NoH);
    float Vis = V_SmithGGXCorrelated(alpha, NoV, NoL);
    vec3 F = F_Schlick(f0, LoH);
    vec3 specular = D * Vis * F;
    
    // Diffuse BRDF
    vec3 diffuse = diffuseColor * Fd_Lambert();
    
    // Energy conservation: diffuse is reduced by Fresnel
    vec3 kD = 1.0 - F; // diffuseColor already includes (1 - metallic)
    
    // Final contribution
    return (kD * diffuse + specular) * lightColor * NoL;
}

// Light attenuation for point/spot lights
float getDistanceAttenuation(float distance, float range) {
    float d2 = distance * distance;
    float rangeAttenuation = sq(saturate(1.0 - sq(distance / range)));
    return rangeAttenuation / (d2 + 1.0);
}

// Spot light cone attenuation
float getSpotAttenuation(vec3 L, vec3 spotDir, float innerCone, float outerCone) {
    float cosAngle = dot(-L, spotDir);
    float attenuation = saturate((cosAngle - outerCone) / (innerCone - outerCone));
    return attenuation * attenuation;
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

// A compact analytic substitute for a prefiltered studio cubemap. Broad panels
// are softened as roughness increases. Like Filament's indirect-light path,
// reflected radiance and diffuse irradiance are evaluated separately: metals
// have no diffuse lobe and must still receive environment specular lighting.
vec3 studioRadiance(vec3 R, float roughness) {
    vec3 radiance = mix(vec3(0.20, 0.23, 0.28), vec3(0.65, 0.72, 0.85),
                        smoothstep(-0.6, 0.8, R.y));
    float exponent = mix(48.0, 2.0, roughness * roughness);
    float panelScale = mix(1.0, 0.55, roughness);
    radiance += vec3(2.8, 2.5, 2.1) * panelScale *
                pow(max(dot(R, normalize(vec3(-0.6, 0.8, 1.0))), 0.0), exponent);
    radiance += vec3(1.0, 1.5, 2.2) * panelScale *
                pow(max(dot(R, normalize(vec3(0.8, 0.3, 0.6))), 0.0), exponent);
    radiance += vec3(1.8) * panelScale *
                pow(max(dot(R, normalize(vec3(0.1, 0.9, -0.8))), 0.0), exponent);
    return radiance;
}

// Standard analytic split-sum GGX DFG fit, avoiding an extra LUT descriptor.
vec2 environmentDFG(float NoV, float roughness) {
    vec4 r = roughness * vec4(-1.0, -0.0275, -0.572, 0.022) +
             vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 evaluateIndirectLight(vec3 N, vec3 V, vec3 diffuseColor, vec3 f0,
                           float roughness, float ao) {
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    vec3 R = normalize(mix(reflect(-V, N), N, roughness * roughness));
    vec2 dfg = environmentDFG(NoV, roughness);
    vec3 specularResponse = max(f0 * dfg.x + dfg.y, vec3(0.0));
    float specularAO = saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
    vec3 irradiance = mix(vec3(0.35, 0.38, 0.43), vec3(0.80, 0.85, 0.95), N.y * 0.5 + 0.5);
    vec3 diffuse = diffuseColor * (1.0 - specularResponse) * irradiance * ao;
    vec3 specular = studioRadiance(R, roughness) * specularResponse * specularAO;
    return diffuse + specular;
}

vec3 toneMap(vec3 color) {
    // Fitted filmic curve preserves highlight roll-off and material contrast.
    return clamp((color * (2.51 * color + 0.03)) /
                 (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

vec3 linearToSRGB(vec3 color) {
    return mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
               step(vec3(0.0031308), color));
}

void main() {
    // Evaluate material parameters
    MaterialParams mat = evaluateMaterial();
    
    // Compute derived material properties
    vec3 diffuseColor = computeDiffuseColor(mat.baseColor, mat.metallic);
    vec3 f0 = computeF0(mat.baseColor, mat.metallic, mat.reflectance);
    
    // Surface normal and view direction
    vec3 N = mat.normal;
    vec3 V = normalize(View.CameraPosition.xyz - inWorldPosition);
    
    // Accumulate lighting
    vec3 Lo = vec3(0.0);
    
    // Process each light
    for (int i = 0; i < Lighting.NumLights && i < MAX_LIGHTS; ++i) {
        LightData light = Lighting.Lights[i];
        int lightType = int(light.Position.w);
        
        vec3 L = N;
        float attenuation = 1.0;
        
        if (lightType == LIGHT_TYPE_DIRECTIONAL) {
            // Directional light: position stores direction
            L = normalize(-light.Position.xyz);
        }
        else if (lightType == LIGHT_TYPE_POINT) {
            // Point light
            vec3 lightVec = light.Position.xyz - inWorldPosition;
            float distance = length(lightVec);
            L = lightVec / max(distance, EPSILON);
            attenuation = getDistanceAttenuation(distance, light.Attenuation.x);
        }
        else if (lightType == LIGHT_TYPE_SPOT) {
            // Spot light
            vec3 lightVec = light.Position.xyz - inWorldPosition;
            float distance = length(lightVec);
            L = lightVec / max(distance, EPSILON);
            attenuation = getDistanceAttenuation(distance, light.Attenuation.x);
            attenuation *= getSpotAttenuation(L, normalize(light.Direction.xyz),
                                              light.Attenuation.y, light.Attenuation.z);
        }
        
        vec3 lightColor = light.Color.rgb * light.Color.w * attenuation;
        Lo += evaluateDirectLight(N, V, L, lightColor, diffuseColor, f0, mat.roughness);
    }
    
    vec3 ambient = evaluateIndirectLight(N, V, diffuseColor, f0, mat.roughness, mat.ao)
                   * Lighting.AmbientIntensity;
    
    // Add emissive
    vec3 emissive = mat.emissive;
    
    // Final color
    vec3 color = Lo + ambient + emissive;
    
    // The demo's UNORM swapchain needs exactly one linear-to-sRGB encoding.
    color = linearToSRGB(toneMap(max(color, vec3(0.0))));
    
    outColor = vec4(color, 1.0);
}
