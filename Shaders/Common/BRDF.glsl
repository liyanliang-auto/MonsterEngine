// Copyright Monster Engine. All Rights Reserved.
// 
// BRDF.glsl - Bidirectional Reflectance Distribution Functions
// 
// This file contains all BRDF functions used for PBR shading.
// Reference: UE5 Engine/Shaders/Private/BRDF.ush
//
// Physically based shading model parameterized with:
// E = Random sample for BRDF
// N = Normal of the macro surface
// H = Normal of the micro surface (half vector)
// V = View vector going from surface's position towards the view's origin
// L = Light ray direction
//
// D = Microfacet NDF (Normal Distribution Function)
// G = Shadowing and masking (Geometry function)
// F = Fresnel
//
// Vis = G / (4*NoL*NoV)
// f = Microfacet specular BRDF = D*G*F / (4*NoL*NoV) = D*Vis*F

#ifndef BRDF_GLSL
#define BRDF_GLSL

// ============================================================================
// Constants
// ============================================================================

#ifndef PI
#define PI 3.14159265359
#endif

#ifndef INV_PI
#define INV_PI 0.31830988618
#endif

#ifndef HALF_PI
#define HALF_PI 1.57079632679
#endif

#ifndef MIN_ROUGHNESS
#define MIN_ROUGHNESS 0.04
#endif

#ifndef MIN_N_DOT_V
#define MIN_N_DOT_V 1e-4
#endif

// ============================================================================
// Utility Functions
// ============================================================================

float Pow2(float x)
{
    return x * x;
}

float Pow4(float x)
{
    float x2 = x * x;
    return x2 * x2;
}

float Pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float Square(float x)
{
    return x * x;
}

vec3 Square(vec3 x)
{
    return x * x;
}

// Fast reciprocal approximation
float rcpFast(float x)
{
    return 1.0 / x;
}

// Fast square root approximation
float sqrtFast(float x)
{
    return sqrt(x);
}

// Clamp roughness to avoid division by zero
float ClampRoughness(float roughness)
{
    return max(roughness, MIN_ROUGHNESS);
}

// ============================================================================
// BxDF Context Structure
// ============================================================================

struct BxDFContext
{
    float NoV;  // dot(N, V)
    float NoL;  // dot(N, L)
    float VoL;  // dot(V, L)
    float NoH;  // dot(N, H)
    float VoH;  // dot(V, H)
    
    // Anisotropic terms
    float XoV;  // dot(X, V) - tangent
    float XoL;  // dot(X, L)
    float XoH;  // dot(X, H)
    float YoV;  // dot(Y, V) - bitangent
    float YoL;  // dot(Y, L)
    float YoH;  // dot(Y, H)
};

// Initialize BxDF context for isotropic materials
void InitBxDFContext(out BxDFContext ctx, vec3 N, vec3 V, vec3 L)
{
    ctx.NoL = dot(N, L);
    ctx.NoV = dot(N, V);
    ctx.VoL = dot(V, L);
    
    float invLenH = inversesqrt(2.0 + 2.0 * ctx.VoL);
    ctx.NoH = clamp((ctx.NoL + ctx.NoV) * invLenH, 0.0, 1.0);
    ctx.VoH = clamp(invLenH + invLenH * ctx.VoL, 0.0, 1.0);
    
    // Zero out anisotropic terms
    ctx.XoV = 0.0;
    ctx.XoL = 0.0;
    ctx.XoH = 0.0;
    ctx.YoV = 0.0;
    ctx.YoL = 0.0;
    ctx.YoH = 0.0;
}

// Initialize BxDF context for anisotropic materials
void InitBxDFContextAniso(out BxDFContext ctx, vec3 N, vec3 X, vec3 Y, vec3 V, vec3 L)
{
    ctx.NoL = dot(N, L);
    ctx.NoV = dot(N, V);
    ctx.VoL = dot(V, L);
    
    float invLenH = inversesqrt(2.0 + 2.0 * ctx.VoL);
    ctx.NoH = clamp((ctx.NoL + ctx.NoV) * invLenH, 0.0, 1.0);
    ctx.VoH = clamp(invLenH + invLenH * ctx.VoL, 0.0, 1.0);
    
    // Anisotropic terms
    ctx.XoV = dot(X, V);
    ctx.XoL = dot(X, L);
    ctx.XoH = (ctx.XoL + ctx.XoV) * invLenH;
    ctx.YoV = dot(Y, V);
    ctx.YoL = dot(Y, L);
    ctx.YoH = (ctx.YoL + ctx.YoV) * invLenH;
}

// ============================================================================
// Diffuse BRDF Functions
// ============================================================================

// Lambert diffuse
// Simple and efficient, energy conserving
vec3 Diffuse_Lambert(vec3 diffuseColor)
{
    return diffuseColor * INV_PI;
}

// [Burley 2012, "Physically-Based Shading at Disney"]
// Disney diffuse with roughness-dependent Fresnel
vec3 Diffuse_Burley(vec3 diffuseColor, float roughness, float NoV, float NoL, float VoH)
{
    float FD90 = 0.5 + 2.0 * VoH * VoH * roughness;
    float FdV = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoV);
    float FdL = 1.0 + (FD90 - 1.0) * Pow5(1.0 - NoL);
    return diffuseColor * (INV_PI * FdV * FdL);
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
// Oren-Nayar approximation
vec3 Diffuse_OrenNayar(vec3 diffuseColor, float roughness, float NoV, float NoL, float VoH)
{
    float a = roughness * roughness;
    float s = a;
    float s2 = s * s;
    float VoL = 2.0 * VoH * VoH - 1.0;  // Double angle identity
    float Cosri = VoL - NoV * NoL;
    float C1 = 1.0 - 0.5 * s2 / (s2 + 0.33);
    float C2 = 0.45 * s2 / (s2 + 0.09) * Cosri * (Cosri >= 0.0 ? 1.0 / max(NoL, NoV) : 1.0);
    return diffuseColor * INV_PI * (C1 + C2) * (1.0 + roughness * 0.5);
}

// [Chan 2018, "Material Advances in Call of Duty: WWII"]
// Advanced diffuse model with retro-reflectivity
vec3 Diffuse_Chan(vec3 diffuseColor, float a2, float NoV, float NoL, float VoH, float NoH, float retroReflectivityWeight)
{
    // Saturate inputs to avoid artifacts
    NoV = clamp(NoV, 0.0, 1.0);
    NoL = clamp(NoL, 0.0, 1.0);
    VoH = clamp(VoH, 0.0, 1.0);
    NoH = clamp(NoH, 0.0, 1.0);
    
    // a2 = 2 / (1 + exp2(18 * g))
    float g = clamp((1.0 / 18.0) * log2(2.0 * rcpFast(a2) - 1.0), 0.0, 1.0);
    
    float F0 = VoH + Pow5(1.0 - VoH);
    float FdV = 1.0 - 0.75 * Pow5(1.0 - NoV);
    float FdL = 1.0 - 0.75 * Pow5(1.0 - NoL);
    
    // Rough (F0) to smooth (FdV * FdL) response interpolation
    float Fd = mix(F0, FdV * FdL, clamp(2.2 * g - 0.5, 0.0, 1.0));
    
    // Retro reflectivity contribution
    float Fb = ((34.5 * g - 59.0) * g + 24.5) * VoH * exp2(-max(73.2 * g - 21.2, 8.9) * sqrtFast(NoH));
    Fb *= retroReflectivityWeight;
    
    return diffuseColor * (INV_PI * (Fd + Fb));
}

// ============================================================================
// Normal Distribution Functions (NDF / D term)
// ============================================================================

// [Blinn 1977, "Models of light reflection for computer synthesized pictures"]
float D_Blinn(float a2, float NoH)
{
    float n = 2.0 / a2 - 2.0;
    return (n + 2.0) / (2.0 * PI) * pow(NoH, n);
}

// [Beckmann 1963, "The scattering of electromagnetic waves from rough surfaces"]
float D_Beckmann(float a2, float NoH)
{
    float NoH2 = NoH * NoH;
    return exp((NoH2 - 1.0) / (a2 * NoH2)) / (PI * a2 * NoH2 * NoH2);
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;  // 2 mad
    return a2 / (PI * d * d);                 // 4 mul, 1 rcp
}

// Anisotropic GGX
// [Burley 2012, "Physically-Based Shading at Disney"]
float D_GGXaniso(float ax, float ay, float NoH, float XoH, float YoH)
{
    float a2 = ax * ay;
    vec3 V = vec3(ay * XoH, ax * YoH, a2 * NoH);
    float S = dot(V, V);
    return INV_PI * a2 * Square(a2 / S);
}

// Inverse GGX for cloth shading
float D_InvGGX(float a2, float NoH)
{
    float A = 4.0;
    float d = (NoH - a2 * NoH) * NoH + a2;
    return 1.0 / (PI * (1.0 + A * a2)) * (1.0 + 4.0 * a2 * a2 / (d * d));
}

// [Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"]
// Charlie distribution for cloth/sheen
float D_Charlie(float roughness, float NoH)
{
    float invR = 1.0 / roughness;
    float cos2H = NoH * NoH;
    float sin2H = 1.0 - cos2H;
    return (2.0 + invR) * pow(sin2H, invR * 0.5) / (2.0 * PI);
}

// ============================================================================
// Visibility / Geometry Functions (G term / Vis = G / (4*NoL*NoV))
// ============================================================================

// Implicit visibility term
float Vis_Implicit()
{
    return 0.25;
}

// [Neumann et al. 1999, "Compact metallic reflectance models"]
float Vis_Neumann(float NoV, float NoL)
{
    return 1.0 / (4.0 * max(NoL, NoV));
}

// [Kelemen 2001, "A microfacet based coupled specular-matte brdf model with importance sampling"]
float Vis_Kelemen(float VoH)
{
    return 1.0 / (4.0 * VoH * VoH + 1e-5);
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
// Tuned to match behavior of Vis_Smith
float Vis_Schlick(float a2, float NoV, float NoL)
{
    float k = sqrt(a2) * 0.5;
    float Vis_SchlickV = NoV * (1.0 - k) + k;
    float Vis_SchlickL = NoL * (1.0 - k) + k;
    return 0.25 / (Vis_SchlickV * Vis_SchlickL);
}

// Smith term for GGX
// [Smith 1967, "Geometrical shadowing of a random rough surface"]
float Vis_Smith(float a2, float NoV, float NoL)
{
    float Vis_SmithV = NoV + sqrt(NoV * (NoV - NoV * a2) + a2);
    float Vis_SmithL = NoL + sqrt(NoL * (NoL - NoL * a2) + a2);
    return 1.0 / (Vis_SmithV * Vis_SmithL);
}

// Approximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (Vis_SmithV + Vis_SmithL);
}

// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
// Exact joint Smith term
float Vis_SmithJoint(float a2, float NoV, float NoL)
{
    float Vis_SmithV = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
    float Vis_SmithL = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
    return 0.5 / (Vis_SmithV + Vis_SmithL);
}

// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
// Anisotropic Smith term
float Vis_SmithJointAniso(float ax, float ay, float NoV, float NoL, float XoV, float XoL, float YoV, float YoL)
{
    float Vis_SmithV = NoL * length(vec3(ax * XoV, ay * YoV, NoV));
    float Vis_SmithL = NoV * length(vec3(ax * XoL, ay * YoL, NoL));
    return 0.5 / (Vis_SmithV + Vis_SmithL);
}

// Cloth visibility term
float Vis_Cloth(float NoV, float NoL)
{
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
}

// Ashikhmin visibility for velvet/cloth
float Vis_Ashikhmin(float NoV, float NoL)
{
    return 1.0 / (4.0 * (NoL + NoV - NoL * NoV));
}

// [Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"]
// Charlie visibility for cloth sheen
float Vis_Charlie_L(float x, float r)
{
    r = clamp(r, 0.0, 1.0);
    r = 1.0 - (1.0 - r) * (1.0 - r);
    
    float a = mix(25.3245, 21.5473, r);
    float b = mix(3.32435, 3.82987, r);
    float c = mix(0.16801, 0.19823, r);
    float d = mix(-1.27393, -1.97760, r);
    float e = mix(-4.85967, -4.32054, r);
    
    return a / ((1.0 + b * pow(x, c)) + d * x + e);
}

float Vis_Charlie(float roughness, float NoV, float NoL)
{
    float VisV = NoV < 0.5 ? exp(Vis_Charlie_L(NoV, roughness)) : exp(2.0 * Vis_Charlie_L(0.5, roughness) - Vis_Charlie_L(1.0 - NoV, roughness));
    float VisL = NoL < 0.5 ? exp(Vis_Charlie_L(NoL, roughness)) : exp(2.0 * Vis_Charlie_L(0.5, roughness) - Vis_Charlie_L(1.0 - NoL, roughness));
    return 1.0 / ((1.0 + VisV + VisL) * (4.0 * NoV * NoL));
}

// ============================================================================
// Fresnel Functions (F term)
// ============================================================================

// No Fresnel (for debugging)
vec3 F_None(vec3 specularColor)
{
    return specularColor;
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
vec3 F_Schlick(vec3 F0, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    return clamp(50.0 * F0.g, 0.0, 1.0) * Fc + (1.0 - Fc) * F0;
}

// Schlick Fresnel with F90 parameter
vec3 F_Schlick(vec3 F0, vec3 F90, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return F90 * Fc + (1.0 - Fc) * F0;
}

// Scalar version of Schlick Fresnel
float F_Schlick(float F0, float F90, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    return F90 * Fc + (1.0 - Fc) * F0;
}

// [Kutz et al. 2021, "Novel aspects of the Adobe Standard Material"]
// Adobe F82 tint model for metals
vec3 F_AdobeF82(vec3 F0, vec3 F82, float VoH)
{
    float Fc = Pow5(1.0 - VoH);
    float K = 49.0 / 46656.0;
    vec3 b = (K - K * F82) * (7776.0 + 9031.0 * F0);
    return clamp(F0 + Fc * ((1.0 - F0) - b * (VoH - VoH * VoH)), vec3(0.0), vec3(1.0));
}

// Full Fresnel equation (more accurate but expensive)
vec3 F_Fresnel(vec3 specularColor, float VoH)
{
    vec3 specularColorSqrt = sqrt(clamp(specularColor, vec3(0.0), vec3(0.99)));
    vec3 n = (1.0 + specularColorSqrt) / (1.0 - specularColorSqrt);
    vec3 g = sqrt(n * n + VoH * VoH - 1.0);
    return 0.5 * Square((g - VoH) / (g + VoH)) * (1.0 + Square(((g + VoH) * VoH - 1.0) / ((g - VoH) * VoH + 1.0)));
}

// ============================================================================
// Environment BRDF (for IBL)
// ============================================================================

// [Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II"]
// Approximation of the preintegrated BRDF for environment lighting
vec2 EnvBRDFApproxLazarov(float roughness, float NoV)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return AB;
}

// Environment BRDF approximation
vec3 EnvBRDFApprox(vec3 specularColor, float roughness, float NoV)
{
    vec2 AB = EnvBRDFApproxLazarov(roughness, NoV);
    
    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    float F90 = clamp(50.0 * specularColor.g, 0.0, 1.0);
    
    return specularColor * AB.x + F90 * AB.y;
}

// Environment BRDF with explicit F0 and F90
vec3 EnvBRDFApprox(vec3 F0, vec3 F90, float roughness, float NoV)
{
    vec2 AB = EnvBRDFApproxLazarov(roughness, NoV);
    return F0 * AB.x + F90 * AB.y;
}

// Non-metal environment BRDF (optimized for dielectrics with F0 = 0.04)
float EnvBRDFApproxNonmetal(float roughness, float NoV)
{
    // Same as EnvBRDFApprox(0.04, roughness, NoV)
    const vec2 c0 = vec2(-1.0, -0.0275);
    const vec2 c1 = vec2(1.0, 0.0425);
    vec2 r = roughness * c0 + c1;
    return min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
}

// ============================================================================
// Anisotropy Utilities
// ============================================================================

// Convert roughness and anisotropy factor into GGX alpha values
// [Kulla 2017, "Revisiting Physically Based Shading at Imageworks"]
void GetAnisotropicRoughness(float alpha, float anisotropy, out float ax, out float ay)
{
    ax = max(alpha * (1.0 + anisotropy), 0.001);
    ay = max(alpha * (1.0 - anisotropy), 0.001);
}

// Alternative: Convert roughness and anisotropy to roughness pair
vec2 GetAnisotropicRoughness(float roughness, float anisotropy)
{
    roughness = clamp(roughness, 0.0, 1.0);
    anisotropy = clamp(anisotropy, -1.0, 1.0);
    float rx = max(roughness * sqrt(1.0 + anisotropy), 0.001);
    float ry = max(roughness * sqrt(1.0 - anisotropy), 0.001);
    return vec2(rx, ry);
}

// ============================================================================
// Specular BRDF Evaluation
// ============================================================================

// Standard isotropic specular GGX BRDF
vec3 SpecularGGX(float roughness, vec3 specularColor, float NoV, float NoL, float NoH, float VoH)
{
    float a2 = Pow4(roughness);
    
    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F = F_Schlick(specularColor, VoH);
    
    return (D * Vis) * F;
}

// Anisotropic specular GGX BRDF
vec3 SpecularGGXAniso(float roughness, float anisotropy, vec3 specularColor, BxDFContext ctx, float NoL)
{
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    
    float ax, ay;
    GetAnisotropicRoughness(alpha, anisotropy, ax, ay);
    
    float D = D_GGXaniso(ax, ay, ctx.NoH, ctx.XoH, ctx.YoH);
    float Vis = Vis_SmithJointAniso(ax, ay, ctx.NoV, NoL, ctx.XoV, ctx.XoL, ctx.YoV, ctx.YoL);
    vec3 F = F_Schlick(specularColor, ctx.VoH);
    
    return (D * Vis) * F;
}

// ============================================================================
// Clear Coat Utilities
// ============================================================================

// Refraction blend factor for clear coat
float RefractBlendClearCoatApprox(float VoH)
{
    // Polynomial approximation for IOR = 1.5 (polyurethane)
    return (0.63 - 0.22 * VoH) * VoH - 0.745;
}

// Simple clear coat transmittance
vec3 SimpleClearCoatTransmittance(float NoL, float NoV, float metallic, vec3 baseColor)
{
    vec3 transmittance = vec3(1.0);
    
    float clearCoatCoverage = metallic;
    if (clearCoatCoverage > 0.0)
    {
        float layerThickness = 1.0;
        float thinDistance = layerThickness * (1.0 / NoV + 1.0 / NoL);
        
        vec3 transmittanceColor = Diffuse_Lambert(baseColor);
        vec3 extinctionCoefficient = -log(max(transmittanceColor, vec3(0.0001))) / (2.0 * layerThickness);
        
        vec3 opticalDepth = extinctionCoefficient * max(thinDistance - 2.0 * layerThickness, 0.0);
        transmittance = exp(-opticalDepth);
        transmittance = mix(vec3(1.0), transmittance, clearCoatCoverage);
    }
    
    return transmittance;
}

// ============================================================================
// Complete BRDF Evaluation
// ============================================================================

// Evaluate complete Cook-Torrance BRDF for a single light
vec3 EvaluateBRDF(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightRadiance,
    vec3 albedo,
    float metallic,
    float roughness,
    vec3 F0)
{
    vec3 H = normalize(V + L);
    
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    // Early out if light is behind surface
    if (NoL <= 0.0)
    {
        return vec3(0.0);
    }
    
    // Clamp roughness
    roughness = ClampRoughness(roughness);
    float a2 = Pow4(roughness);
    
    // Calculate BRDF components
    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F = F_Schlick(F0, VoH);
    
    // Specular BRDF
    vec3 specular = (D * Vis) * F;
    
    // Diffuse BRDF (energy conserving)
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 diffuse = kD * Diffuse_Lambert(albedo);
    
    // Combine and apply light
    return (diffuse + specular) * lightRadiance * NoL;
}

// Evaluate BRDF with Burley diffuse
vec3 EvaluateBRDFBurley(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 lightRadiance,
    vec3 albedo,
    float metallic,
    float roughness,
    vec3 F0)
{
    vec3 H = normalize(V + L);
    
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), MIN_N_DOT_V);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    
    if (NoL <= 0.0)
    {
        return vec3(0.0);
    }
    
    roughness = ClampRoughness(roughness);
    float a2 = Pow4(roughness);
    
    // Specular
    float D = D_GGX(a2, NoH);
    float Vis = Vis_SmithJointApprox(a2, NoV, NoL);
    vec3 F = F_Schlick(F0, VoH);
    vec3 specular = (D * Vis) * F;
    
    // Diffuse (Burley)
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 diffuse = kD * Diffuse_Burley(albedo, roughness, NoV, NoL, VoH);
    
    return (diffuse + specular) * lightRadiance * NoL;
}

#endif // BRDF_GLSL
