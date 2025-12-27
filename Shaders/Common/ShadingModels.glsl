// Copyright Monster Engine. All Rights Reserved.
// 
// ShadingModels.glsl - Shading Model Implementations
// 
// This file contains shading model implementations for PBR rendering.
// Reference: UE5 Engine/Shaders/Private/ShadingModels.ush
//
// Supported Shading Models:
// - Unlit: No lighting calculations
// - DefaultLit: Standard PBR metallic-roughness workflow
// - Subsurface: Simple subsurface scattering approximation
// - ClearCoat: Dual-layer BRDF for car paint, lacquered surfaces
// - Cloth: Sheen BRDF for fabric materials
// - TwoSidedFoliage: Transmission for leaves and thin surfaces

#ifndef SHADING_MODELS_GLSL
#define SHADING_MODELS_GLSL

#include "BRDF.glsl"

// ============================================================================
// Shading Model IDs
// ============================================================================

#define SHADING_MODEL_UNLIT              0
#define SHADING_MODEL_DEFAULT_LIT        1
#define SHADING_MODEL_SUBSURFACE         2
#define SHADING_MODEL_PREINTEGRATED_SKIN 3
#define SHADING_MODEL_CLEAR_COAT         4
#define SHADING_MODEL_SUBSURFACE_PROFILE 5
#define SHADING_MODEL_TWO_SIDED_FOLIAGE  6
#define SHADING_MODEL_HAIR               7
#define SHADING_MODEL_CLOTH              8
#define SHADING_MODEL_EYE                9
#define SHADING_MODEL_SINGLE_LAYER_WATER 10
#define SHADING_MODEL_THIN_TRANSLUCENT   11

// ============================================================================
// Direct Lighting Result Structure
// ============================================================================

struct FDirectLighting
{
    vec3 Diffuse;
    vec3 Specular;
    vec3 Transmission;
};

// Initialize direct lighting to zero
FDirectLighting InitDirectLighting()
{
    FDirectLighting lighting;
    lighting.Diffuse = vec3(0.0);
    lighting.Specular = vec3(0.0);
    lighting.Transmission = vec3(0.0);
    return lighting;
}

// ============================================================================
// Material Data Structure
// ============================================================================

struct FMaterialData
{
    vec3  BaseColor;
    float Metallic;
    float Roughness;
    vec3  Normal;
    float AmbientOcclusion;
    vec3  EmissiveColor;
    float Opacity;
    
    // Derived values
    vec3  DiffuseColor;
    vec3  SpecularColor;
    vec3  F0;
    
    // Shading model specific
    int   ShadingModel;
    vec3  SubsurfaceColor;
    float ClearCoat;
    float ClearCoatRoughness;
    float Anisotropy;
    vec3  Tangent;
    float Cloth;
    vec3  FuzzColor;
};

// Initialize material data with PBR defaults
void InitMaterialData(out FMaterialData mat)
{
    mat.BaseColor = vec3(0.5);
    mat.Metallic = 0.0;
    mat.Roughness = 0.5;
    mat.Normal = vec3(0.0, 0.0, 1.0);
    mat.AmbientOcclusion = 1.0;
    mat.EmissiveColor = vec3(0.0);
    mat.Opacity = 1.0;
    
    mat.DiffuseColor = vec3(0.5);
    mat.SpecularColor = vec3(0.04);
    mat.F0 = vec3(0.04);
    
    mat.ShadingModel = SHADING_MODEL_DEFAULT_LIT;
    mat.SubsurfaceColor = vec3(0.0);
    mat.ClearCoat = 0.0;
    mat.ClearCoatRoughness = 0.1;
    mat.Anisotropy = 0.0;
    mat.Tangent = vec3(1.0, 0.0, 0.0);
    mat.Cloth = 0.0;
    mat.FuzzColor = vec3(0.0);
}

// Compute derived material properties
void ComputeDerivedMaterialProperties(inout FMaterialData mat)
{
    // Dielectric F0 (4% reflectance for non-metals)
    vec3 dielectricF0 = vec3(0.04);
    
    // For metals, F0 is the base color
    mat.F0 = mix(dielectricF0, mat.BaseColor, mat.Metallic);
    
    // Diffuse color (metals have no diffuse)
    mat.DiffuseColor = mat.BaseColor * (1.0 - mat.Metallic);
    
    // Specular color
    mat.SpecularColor = mat.F0;
}

// ============================================================================
// Light Data Structure
// ============================================================================

struct FLightData
{
    vec3  Direction;      // Light direction (towards light)
    vec3  Color;          // Light color * intensity
    float Attenuation;    // Distance attenuation
    float NoL;            // Clamped N dot L
};

// ============================================================================
// Unlit Shading Model
// ============================================================================

FDirectLighting ShadingModel_Unlit(FMaterialData mat)
{
    FDirectLighting lighting = InitDirectLighting();
    // Unlit materials only use emissive
    // The emissive is added separately in the final composition
    return lighting;
}

// ============================================================================
// Default Lit Shading Model (Standard PBR)
// ============================================================================

FDirectLighting ShadingModel_DefaultLit(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightColor,
    float attenuation,
    float NoL)
{
    FDirectLighting lighting = InitDirectLighting();
    
    // Early out if light is behind surface
    if (NoL <= 0.0)
    {
        return lighting;
    }
    
    // Initialize BxDF context
    BxDFContext ctx;
    InitBxDFContext(ctx, N, V, L);
    ctx.NoV = clamp(abs(ctx.NoV) + 1e-5, 0.0, 1.0);
    
    float roughness = ClampRoughness(mat.Roughness);
    float a2 = Pow4(roughness);
    
    // Diffuse
    lighting.Diffuse = Diffuse_Lambert(mat.DiffuseColor);
    lighting.Diffuse *= lightColor * (attenuation * NoL);
    
    // Specular GGX
    float D = D_GGX(a2, ctx.NoH);
    float Vis = Vis_SmithJointApprox(a2, ctx.NoV, NoL);
    vec3 F = F_Schlick(mat.SpecularColor, ctx.VoH);
    
    lighting.Specular = (D * Vis) * F;
    lighting.Specular *= lightColor * (attenuation * NoL);
    
    return lighting;
}

// DefaultLit with anisotropy support
FDirectLighting ShadingModel_DefaultLitAniso(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 X,
    vec3 Y,
    vec3 lightColor,
    float attenuation,
    float NoL)
{
    FDirectLighting lighting = InitDirectLighting();
    
    if (NoL <= 0.0)
    {
        return lighting;
    }
    
    // Initialize anisotropic BxDF context
    BxDFContext ctx;
    InitBxDFContextAniso(ctx, N, X, Y, V, L);
    ctx.NoV = clamp(abs(ctx.NoV) + 1e-5, 0.0, 1.0);
    
    float roughness = ClampRoughness(mat.Roughness);
    float alpha = roughness * roughness;
    
    // Diffuse
    lighting.Diffuse = Diffuse_Lambert(mat.DiffuseColor);
    lighting.Diffuse *= lightColor * (attenuation * NoL);
    
    // Anisotropic specular
    float ax, ay;
    GetAnisotropicRoughness(alpha, mat.Anisotropy, ax, ay);
    
    float D = D_GGXaniso(ax, ay, ctx.NoH, ctx.XoH, ctx.YoH);
    float Vis = Vis_SmithJointAniso(ax, ay, ctx.NoV, NoL, ctx.XoV, ctx.XoL, ctx.YoV, ctx.YoL);
    vec3 F = F_Schlick(mat.SpecularColor, ctx.VoH);
    
    lighting.Specular = (D * Vis) * F;
    lighting.Specular *= lightColor * (attenuation * NoL);
    
    return lighting;
}

// ============================================================================
// Subsurface Shading Model
// ============================================================================

FDirectLighting ShadingModel_Subsurface(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightColor,
    float attenuation,
    float NoL)
{
    // Start with default lit
    FDirectLighting lighting = ShadingModel_DefaultLit(mat, N, V, L, lightColor, attenuation, NoL);
    
    float opacity = mat.Opacity;
    vec3 subsurfaceColor = mat.SubsurfaceColor;
    
    // In-scatter: view-dependent effect when looking through the material
    float inScatter = pow(clamp(dot(L, -V), 0.0, 1.0), 12.0) * mix(3.0, 0.1, opacity);
    
    // Wrap lighting for subsurface effect
    // Simplified version with w=0.5, n=1.5
    float wrappedDiffuse = pow(clamp(dot(N, L) / 1.5 + 0.5 / 1.5, 0.0, 1.0), 1.5) * (2.5 / 1.5);
    float normalContribution = mix(1.0, wrappedDiffuse, opacity);
    float backScatter = normalContribution / (PI * 2.0);
    
    // Transmission
    lighting.Transmission = lightColor * (attenuation * mix(backScatter, 1.0, inScatter)) * subsurfaceColor;
    
    return lighting;
}

// ============================================================================
// Clear Coat Shading Model
// ============================================================================

FDirectLighting ShadingModel_ClearCoat(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightColor,
    float attenuation,
    float NoL)
{
    FDirectLighting lighting = InitDirectLighting();
    
    if (NoL <= 0.0)
    {
        return lighting;
    }
    
    float clearCoat = mat.ClearCoat;
    float clearCoatRoughness = max(mat.ClearCoatRoughness, 0.02);
    
    // Initialize context
    BxDFContext ctx;
    InitBxDFContext(ctx, N, V, L);
    ctx.NoV = clamp(abs(ctx.NoV) + 1e-5, 0.0, 1.0);
    
    // ========================================
    // Top Layer (Clear Coat)
    // ========================================
    
    // Hard-coded Fresnel with IOR = 1.5 (polyurethane)
    float F0_coat = 0.04;
    float Fc = Pow5(1.0 - ctx.VoH);
    float F_coat = Fc + (1.0 - Fc) * F0_coat;
    
    // Clear coat specular
    float alpha_coat = clearCoatRoughness * clearCoatRoughness;
    float a2_coat = alpha_coat * alpha_coat;
    
    float D_coat = D_GGX(a2_coat, ctx.NoH);
    float Vis_coat = Vis_SmithJointApprox(a2_coat, ctx.NoV, NoL);
    
    vec3 specular_coat = vec3(clearCoat * D_coat * Vis_coat * F_coat);
    specular_coat *= lightColor * (attenuation * NoL);
    
    // Fresnel attenuation for bottom layer
    float fresnelCoeff = (1.0 - F_coat);
    fresnelCoeff *= fresnelCoeff;
    
    // ========================================
    // Bottom Layer (Base Material)
    // ========================================
    
    float roughness = ClampRoughness(mat.Roughness);
    float a2 = Pow4(roughness);
    
    // Diffuse
    vec3 diffuse_base = Diffuse_Lambert(mat.DiffuseColor);
    vec3 diffuse_refracted = fresnelCoeff * diffuse_base;
    lighting.Diffuse = mix(diffuse_base, diffuse_refracted, clearCoat);
    lighting.Diffuse *= lightColor * (attenuation * NoL);
    
    // Specular
    float D_base = D_GGX(a2, ctx.NoH);
    float Vis_base = Vis_SmithJointApprox(a2, ctx.NoV, NoL);
    vec3 F_base = F_Schlick(mat.SpecularColor, ctx.VoH);
    
    vec3 specular_base = (D_base * Vis_base) * F_base;
    vec3 specular_refracted = fresnelCoeff * specular_base;
    
    lighting.Specular = specular_coat + mix(specular_base, specular_refracted, clearCoat) * lightColor * (attenuation * NoL);
    
    return lighting;
}

// ============================================================================
// Cloth Shading Model
// ============================================================================

FDirectLighting ShadingModel_Cloth(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightColor,
    float attenuation,
    float NoL)
{
    FDirectLighting lighting = InitDirectLighting();
    
    if (NoL <= 0.0)
    {
        return lighting;
    }
    
    vec3 fuzzColor = mat.FuzzColor;
    float cloth = clamp(mat.Cloth, 0.0, 1.0);
    
    // Initialize context
    BxDFContext ctx;
    InitBxDFContext(ctx, N, V, L);
    ctx.NoV = clamp(abs(ctx.NoV) + 1e-5, 0.0, 1.0);
    
    float roughness = ClampRoughness(mat.Roughness);
    float a2 = Pow4(roughness);
    
    // Diffuse
    lighting.Diffuse = Diffuse_Lambert(mat.DiffuseColor);
    lighting.Diffuse *= lightColor * (attenuation * NoL);
    
    // Standard GGX specular (Spec1)
    float D1 = D_GGX(a2, ctx.NoH);
    float Vis1 = Vis_SmithJointApprox(a2, ctx.NoV, NoL);
    vec3 F1 = F_Schlick(mat.SpecularColor, ctx.VoH);
    vec3 Spec1 = (D1 * Vis1) * F1;
    
    // Cloth specular - Inverse Beckmann/GGX for sheen (Spec2)
    float D2 = D_InvGGX(a2, ctx.NoH);
    float Vis2 = Vis_Cloth(ctx.NoV, NoL);
    vec3 F2 = F_Schlick(fuzzColor, ctx.VoH);
    vec3 Spec2 = (D2 * Vis2) * F2;
    
    // Blend between standard and cloth specular
    lighting.Specular = mix(Spec1, Spec2, cloth);
    lighting.Specular *= lightColor * (attenuation * NoL);
    
    return lighting;
}

// ============================================================================
// Two-Sided Foliage Shading Model
// ============================================================================

FDirectLighting ShadingModel_TwoSidedFoliage(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightColor,
    float attenuation,
    float NoL)
{
    // Start with default lit
    FDirectLighting lighting = ShadingModel_DefaultLit(mat, N, V, L, lightColor, attenuation, NoL);
    
    vec3 subsurfaceColor = mat.SubsurfaceColor;
    
    // Wrap lighting for transmission
    // http://blog.stevemcauley.com/2011/12/03/energy-conserving-wrapped-diffuse/
    float wrap = 0.5;
    float wrapNoL = clamp((-dot(N, L) + wrap) / Square(1.0 + wrap), 0.0, 1.0);
    
    // Scatter distribution
    float VoL = dot(V, L);
    float scatter = D_GGX(0.36, clamp(-VoL, 0.0, 1.0));  // 0.6^2 = 0.36
    
    lighting.Transmission = lightColor * (attenuation * wrapNoL * scatter) * subsurfaceColor;
    
    return lighting;
}

// ============================================================================
// Simple Shading (for Lumen-style indirect lighting)
// ============================================================================

vec3 SimpleShading(vec3 diffuseColor, vec3 specularColor, float roughness, vec3 L, vec3 V, vec3 N)
{
    vec3 H = normalize(V + L);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    
    // Simple specular
    float D = D_GGX(Pow4(roughness), NoH);
    float Vis = Vis_Implicit();
    vec3 F = F_None(specularColor);
    
    return Diffuse_Lambert(diffuseColor) + (D * Vis) * F;
}

// ============================================================================
// Shading Model Dispatcher
// ============================================================================

FDirectLighting EvaluateShadingModel(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightColor,
    float attenuation,
    float NoL)
{
    FDirectLighting lighting = InitDirectLighting();
    
    switch (mat.ShadingModel)
    {
        case SHADING_MODEL_UNLIT:
            lighting = ShadingModel_Unlit(mat);
            break;
            
        case SHADING_MODEL_DEFAULT_LIT:
            lighting = ShadingModel_DefaultLit(mat, N, V, L, lightColor, attenuation, NoL);
            break;
            
        case SHADING_MODEL_SUBSURFACE:
        case SHADING_MODEL_PREINTEGRATED_SKIN:
        case SHADING_MODEL_SUBSURFACE_PROFILE:
            lighting = ShadingModel_Subsurface(mat, N, V, L, lightColor, attenuation, NoL);
            break;
            
        case SHADING_MODEL_CLEAR_COAT:
            lighting = ShadingModel_ClearCoat(mat, N, V, L, lightColor, attenuation, NoL);
            break;
            
        case SHADING_MODEL_TWO_SIDED_FOLIAGE:
            lighting = ShadingModel_TwoSidedFoliage(mat, N, V, L, lightColor, attenuation, NoL);
            break;
            
        case SHADING_MODEL_CLOTH:
            lighting = ShadingModel_Cloth(mat, N, V, L, lightColor, attenuation, NoL);
            break;
            
        default:
            // Fall back to default lit
            lighting = ShadingModel_DefaultLit(mat, N, V, L, lightColor, attenuation, NoL);
            break;
    }
    
    return lighting;
}

// ============================================================================
// Environment / IBL Shading
// ============================================================================

// Evaluate environment lighting contribution
vec3 EvaluateEnvironmentBRDF(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 irradiance,
    vec3 prefilteredColor,
    float NoV)
{
    float roughness = ClampRoughness(mat.Roughness);
    
    // Diffuse IBL
    vec3 kS = F_Schlick(mat.F0, max(NoV, 0.0));
    vec3 kD = (1.0 - kS) * (1.0 - mat.Metallic);
    vec3 diffuseIBL = kD * irradiance * mat.DiffuseColor;
    
    // Specular IBL
    vec3 envBRDF = EnvBRDFApprox(mat.SpecularColor, roughness, NoV);
    vec3 specularIBL = prefilteredColor * envBRDF;
    
    return (diffuseIBL + specularIBL) * mat.AmbientOcclusion;
}

// Evaluate environment lighting for clear coat
vec3 EvaluateEnvironmentBRDF_ClearCoat(
    FMaterialData mat,
    vec3 N,
    vec3 V,
    vec3 irradiance,
    vec3 prefilteredColor,
    vec3 prefilteredColorCoat,
    float NoV)
{
    float roughness = ClampRoughness(mat.Roughness);
    float clearCoatRoughness = max(mat.ClearCoatRoughness, 0.02);
    float clearCoat = mat.ClearCoat;
    
    // Clear coat Fresnel
    float F0_coat = 0.04;
    float Fc = Pow5(1.0 - NoV);
    float F_coat = Fc + (1.0 - Fc) * F0_coat;
    
    // Clear coat specular IBL
    vec2 envBRDF_coat = EnvBRDFApproxLazarov(clearCoatRoughness, NoV);
    vec3 specularIBL_coat = prefilteredColorCoat * (F0_coat * envBRDF_coat.x + envBRDF_coat.y);
    
    // Fresnel attenuation for base layer
    float fresnelCoeff = (1.0 - F_coat);
    fresnelCoeff *= fresnelCoeff;
    
    // Base layer
    vec3 kS = F_Schlick(mat.F0, NoV);
    vec3 kD = (1.0 - kS) * (1.0 - mat.Metallic);
    
    vec3 diffuseIBL = kD * irradiance * mat.DiffuseColor;
    vec3 envBRDF = EnvBRDFApprox(mat.SpecularColor, roughness, NoV);
    vec3 specularIBL = prefilteredColor * envBRDF;
    
    // Combine with clear coat attenuation
    vec3 baseLayer = mix(diffuseIBL + specularIBL, fresnelCoeff * (diffuseIBL + specularIBL), clearCoat);
    vec3 coatLayer = clearCoat * specularIBL_coat;
    
    return (baseLayer + coatLayer) * mat.AmbientOcclusion;
}

// ============================================================================
// Utility Functions
// ============================================================================

// Get shading model name for debugging
// Note: This is for CPU-side use, not in shaders
#ifdef SHADING_MODEL_DEBUG
const char* GetShadingModelName(int shadingModel)
{
    switch (shadingModel)
    {
        case SHADING_MODEL_UNLIT:              return "Unlit";
        case SHADING_MODEL_DEFAULT_LIT:        return "DefaultLit";
        case SHADING_MODEL_SUBSURFACE:         return "Subsurface";
        case SHADING_MODEL_PREINTEGRATED_SKIN: return "PreintegratedSkin";
        case SHADING_MODEL_CLEAR_COAT:         return "ClearCoat";
        case SHADING_MODEL_SUBSURFACE_PROFILE: return "SubsurfaceProfile";
        case SHADING_MODEL_TWO_SIDED_FOLIAGE:  return "TwoSidedFoliage";
        case SHADING_MODEL_HAIR:               return "Hair";
        case SHADING_MODEL_CLOTH:              return "Cloth";
        case SHADING_MODEL_EYE:                return "Eye";
        case SHADING_MODEL_SINGLE_LAYER_WATER: return "SingleLayerWater";
        case SHADING_MODEL_THIN_TRANSLUCENT:   return "ThinTranslucent";
        default:                               return "Unknown";
    }
}
#endif

#endif // SHADING_MODELS_GLSL
