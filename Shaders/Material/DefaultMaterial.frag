/**
 * Default Material Pixel Shader
 * 
 * Standard PBR pixel shader with:
 * - Base color (albedo)
 * - Metallic/Roughness workflow
 * - Normal mapping
 * - Simple directional lighting
 */

#version 450

// ============================================================================
// Fragment Inputs
// ============================================================================

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;

// ============================================================================
// Fragment Outputs
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniforms (set 0, binding 0)
layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 viewProjMatrix;
    vec4 cameraPosition;    // xyz = position, w = unused
    vec4 cameraDirection;   // xyz = direction, w = unused
} view;

// Material parameters (set 2, binding 0)
layout(set = 2, binding = 0) uniform MaterialParams {
    vec4 baseColor;         // RGB = color, A = alpha
    vec4 emissiveColor;     // RGB = emissive, A = intensity
    float metallic;
    float roughness;
    float specular;
    float opacity;
    float normalScale;
    float aoStrength;
    float padding1;
    float padding2;
} material;

// ============================================================================
// Textures
// ============================================================================

layout(set = 2, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 2, binding = 2) uniform sampler2D normalTexture;
layout(set = 2, binding = 3) uniform sampler2D metallicRoughnessTexture;
layout(set = 2, binding = 4) uniform sampler2D emissiveTexture;
layout(set = 2, binding = 5) uniform sampler2D aoTexture;

// ============================================================================
// Constants
// ============================================================================

const float PI = 3.14159265359;
const vec3 LIGHT_DIR = normalize(vec3(1.0, 1.0, 0.5));
const vec3 LIGHT_COLOR = vec3(1.0, 0.98, 0.95);
const float LIGHT_INTENSITY = 2.0;
const vec3 AMBIENT_COLOR = vec3(0.03);

// ============================================================================
// PBR Functions
// ============================================================================

/**
 * Normal Distribution Function (GGX/Trowbridge-Reitz)
 */
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    
    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return nom / max(denom, 0.0001);
}

/**
 * Geometry Function (Schlick-GGX)
 */
float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / max(denom, 0.0001);
}

/**
 * Geometry Function (Smith)
 */
float G_Smith(float NdotV, float NdotL, float roughness)
{
    float ggx1 = G_SchlickGGX(NdotV, roughness);
    float ggx2 = G_SchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

/**
 * Fresnel Function (Schlick approximation)
 */
vec3 F_Schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Sample textures
    vec4 baseColorSample = texture(baseColorTexture, fragTexCoord);
    vec4 metallicRoughnessSample = texture(metallicRoughnessTexture, fragTexCoord);
    vec3 normalSample = texture(normalTexture, fragTexCoord).rgb;
    vec3 emissiveSample = texture(emissiveTexture, fragTexCoord).rgb;
    float aoSample = texture(aoTexture, fragTexCoord).r;
    
    // Calculate final material properties
    vec3 albedo = material.baseColor.rgb * baseColorSample.rgb;
    float metallic = material.metallic * metallicRoughnessSample.b;
    float roughness = material.roughness * metallicRoughnessSample.g;
    float ao = mix(1.0, aoSample, material.aoStrength);
    vec3 emissive = material.emissiveColor.rgb * emissiveSample * material.emissiveColor.a;
    
    // Calculate normal from normal map
    vec3 N = normalize(fragNormal);
    if (length(normalSample) > 0.01)
    {
        vec3 tangentNormal = normalSample * 2.0 - 1.0;
        tangentNormal.xy *= material.normalScale;
        
        mat3 TBN = mat3(
            normalize(fragTangent),
            normalize(fragBitangent),
            N
        );
        N = normalize(TBN * tangentNormal);
    }
    
    // View direction
    vec3 V = normalize(view.cameraPosition.xyz - fragWorldPos);
    
    // Light direction
    vec3 L = LIGHT_DIR;
    vec3 H = normalize(V + L);
    
    // Dot products
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    
    // Calculate F0 (reflectance at normal incidence)
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Cook-Torrance BRDF
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);
    vec3 F = F_Schlick(HdotV, F0);
    
    vec3 numerator = D * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;
    
    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    
    // Final radiance
    vec3 radiance = LIGHT_COLOR * LIGHT_INTENSITY;
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;
    
    // Ambient lighting
    vec3 ambient = AMBIENT_COLOR * albedo * ao;
    
    // Final color
    vec3 color = ambient + Lo + emissive;
    
    // Tone mapping (simple Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    // Output
    float alpha = material.baseColor.a * baseColorSample.a * material.opacity;
    outColor = vec4(color, alpha);
}
