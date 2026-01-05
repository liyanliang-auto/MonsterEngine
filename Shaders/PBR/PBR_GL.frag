//------------------------------------------------------------------------------
// PBR_GL.frag - PBR Fragment Shader (OpenGL GLSL)
//------------------------------------------------------------------------------
// Copyright Monster Engine. All Rights Reserved.
// Reference: Google Filament, UE5 BasePassPixelShader
//
// Implements physically based rendering with:
// - GGX specular BRDF (D_GGX, V_SmithGGXCorrelated, F_Schlick)
// - Lambertian diffuse
// - Direct lighting (point, directional, spot lights)
// - Simple ambient lighting (IBL to be added later)
//
// Row-major matrices, row vector multiplication (UE5 convention)
// Non-reverse Z depth buffer
//------------------------------------------------------------------------------

#version 330 core

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define PI 3.14159265358979323846
#define INV_PI 0.31830988618379067154
#define EPSILON 1e-6
#define MIN_ROUGHNESS 0.045
#define MIN_N_DOT_V 1e-4
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

float D_GGX(float alpha, float NoH) {
    float a2 = alpha * alpha;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d + EPSILON);
}

float V_SmithGGXCorrelated(float alpha, float NoV, float NoL) {
    float a2 = alpha * alpha;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    return 0.5 / (lambdaV + lambdaL + EPSILON);
}

vec3 F_Schlick(vec3 f0, float VoH) {
    return f0 + (1.0 - f0) * pow5(1.0 - VoH);
}

float Fd_Lambert() {
    return INV_PI;
}

//------------------------------------------------------------------------------
// Uniforms
//------------------------------------------------------------------------------

// View uniforms
uniform vec3 u_CameraPosition;

// Light structure
struct LightData {
    vec4 Position;        // xyz = position/direction, w = type
    vec4 Color;           // xyz = color, w = intensity
    vec4 Direction;       // xyz = direction (spot), w = unused
    vec4 Attenuation;     // x = range, y = inner cone, z = outer cone, w = unused
};

uniform LightData u_Lights[MAX_LIGHTS];
uniform int u_NumLights;
uniform float u_AmbientIntensity;

// Material uniforms
uniform vec4 u_BaseColorFactor;
uniform vec4 u_EmissiveFactor;
uniform float u_MetallicFactor;
uniform float u_RoughnessFactor;
uniform float u_ReflectanceFactor;
uniform float u_AmbientOcclusion;
uniform float u_AlphaCutoff;

// Material textures
uniform sampler2D u_BaseColorMap;
uniform sampler2D u_NormalMap;
uniform sampler2D u_MetallicRoughnessMap;
uniform sampler2D u_OcclusionMap;
uniform sampler2D u_EmissiveMap;

//------------------------------------------------------------------------------
// Fragment Inputs
//------------------------------------------------------------------------------

in vec3 v_WorldPosition;
in vec3 v_WorldNormal;
in vec4 v_WorldTangent;
in vec2 v_TexCoord0;
in vec2 v_TexCoord1;
in vec4 v_VertexColor;
in vec3 v_ViewDirection;

//------------------------------------------------------------------------------
// Fragment Outputs
//------------------------------------------------------------------------------

out vec4 FragColor;

//------------------------------------------------------------------------------
// Normal Mapping
//------------------------------------------------------------------------------

vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(u_NormalMap, v_TexCoord0).xyz * 2.0 - 1.0;
    
    vec3 N = normalize(v_WorldNormal);
    vec3 T = normalize(v_WorldTangent.xyz);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * v_WorldTangent.w;
    
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
    
    vec4 baseColorSample = texture(u_BaseColorMap, v_TexCoord0);
    params.baseColor = baseColorSample.rgb * u_BaseColorFactor.rgb * v_VertexColor.rgb;
    
    vec4 mrSample = texture(u_MetallicRoughnessMap, v_TexCoord0);
    params.roughness = mrSample.g * u_RoughnessFactor;
    params.metallic = mrSample.b * u_MetallicFactor;
    params.roughness = max(params.roughness, MIN_ROUGHNESS);
    
    params.reflectance = u_ReflectanceFactor;
    params.ao = texture(u_OcclusionMap, v_TexCoord0).r * u_AmbientOcclusion;
    params.emissive = texture(u_EmissiveMap, v_TexCoord0).rgb * u_EmissiveFactor.rgb;
    params.normal = getNormalFromMap();
    
    return params;
}

//------------------------------------------------------------------------------
// Lighting Calculation
//------------------------------------------------------------------------------

vec3 computeF0(vec3 baseColor, float metallic, float reflectance) {
    vec3 dielectricF0 = vec3(0.16 * reflectance * reflectance);
    return mix(dielectricF0, baseColor, metallic);
}

vec3 computeDiffuseColor(vec3 baseColor, float metallic) {
    return baseColor * (1.0 - metallic);
}

vec3 evaluateDirectLight(
    vec3 N, vec3 V, vec3 L, vec3 lightColor,
    vec3 diffuseColor, vec3 f0, float roughness
) {
    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0) return vec3(0.0);
    
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    vec3 H = normalize(V + L);
    float NoH = saturate(dot(N, H));
    float LoH = saturate(dot(L, H));
    
    float alpha = roughness * roughness;
    
    float D = D_GGX(alpha, NoH);
    float Vis = V_SmithGGXCorrelated(alpha, NoV, NoL);
    vec3 F = F_Schlick(f0, LoH);
    vec3 specular = D * Vis * F;
    
    vec3 diffuse = diffuseColor * Fd_Lambert();
    vec3 kD = (1.0 - F) * (1.0 - f0.r);
    
    return (kD * diffuse + specular) * lightColor * NoL;
}

float getDistanceAttenuation(float distance, float range) {
    float d2 = distance * distance;
    float rangeAttenuation = sq(saturate(1.0 - sq(distance / range)));
    return rangeAttenuation / (d2 + 1.0);
}

float getSpotAttenuation(vec3 L, vec3 spotDir, float innerCone, float outerCone) {
    float cosAngle = dot(-L, spotDir);
    float attenuation = saturate((cosAngle - outerCone) / (innerCone - outerCone));
    return attenuation * attenuation;
}

//------------------------------------------------------------------------------
// Main
//------------------------------------------------------------------------------

void main() {
    MaterialParams mat = evaluateMaterial();
    
    vec3 diffuseColor = computeDiffuseColor(mat.baseColor, mat.metallic);
    vec3 f0 = computeF0(mat.baseColor, mat.metallic, mat.reflectance);
    
    vec3 N = mat.normal;
    vec3 V = normalize(v_ViewDirection);
    
    vec3 Lo = vec3(0.0);
    
    for (int i = 0; i < u_NumLights && i < MAX_LIGHTS; ++i) {
        LightData light = u_Lights[i];
        int lightType = int(light.Position.w);
        
        vec3 L;
        float attenuation = 1.0;
        
        if (lightType == 0) { // Directional
            L = normalize(-light.Position.xyz);
        }
        else if (lightType == 1) { // Point
            vec3 lightVec = light.Position.xyz - v_WorldPosition;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = getDistanceAttenuation(distance, light.Attenuation.x);
        }
        else if (lightType == 2) { // Spot
            vec3 lightVec = light.Position.xyz - v_WorldPosition;
            float distance = length(lightVec);
            L = lightVec / distance;
            attenuation = getDistanceAttenuation(distance, light.Attenuation.x);
            attenuation *= getSpotAttenuation(L, normalize(light.Direction.xyz),
                                              light.Attenuation.y, light.Attenuation.z);
        }
        
        vec3 lightColor = light.Color.rgb * light.Color.w * attenuation;
        Lo += evaluateDirectLight(N, V, L, lightColor, diffuseColor, f0, mat.roughness);
    }
    
    vec3 ambient = diffuseColor * u_AmbientIntensity * mat.ao;
    vec3 emissive = mat.emissive;
    vec3 color = Lo + ambient + emissive;
    
    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));
    
    FragColor = vec4(color, 1.0);
}
