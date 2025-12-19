// Copyright Monster Engine. All Rights Reserved.
// Shadow Common Functions
// 
// Shared shadow mapping functions for all shadow-related shaders.
// Includes PCF filtering, bias calculation, and shadow fade.
//
// Reference: UE5 ShadowFilteringCommon.ush, ShadowProjectionCommon.ush

#ifndef SHADOW_COMMON_GLSL
#define SHADOW_COMMON_GLSL

// ============================================================================
// Constants
// ============================================================================

const float SHADOW_DEPTH_BIAS_DEFAULT = 0.005;
const float SHADOW_SLOPE_BIAS_DEFAULT = 0.01;
const float SHADOW_NORMAL_BIAS_DEFAULT = 0.02;
const float SHADOW_MAX_DISTANCE = 100.0;

// ============================================================================
// Shadow Bias Calculation
// ============================================================================

// Calculate depth bias based on surface normal and light direction
// This helps reduce shadow acne while minimizing peter-panning
float CalculateShadowBias(
    float constantBias,
    float slopeBias,
    float maxSlopeBias,
    vec3 normal,
    vec3 lightDir)
{
    // Calculate the slope factor (how perpendicular the surface is to light)
    float NoL = abs(dot(normal, lightDir));
    float slope = sqrt(1.0 - NoL * NoL) / max(NoL, 0.0001);
    slope = min(slope, maxSlopeBias);
    
    // Combine constant and slope-scaled bias
    return constantBias + slopeBias * slope;
}

// Calculate normal offset bias
// Offsets the shadow sample position along the surface normal
vec3 ApplyNormalOffsetBias(
    vec3 worldPos,
    vec3 normal,
    float normalBias,
    vec3 lightDir)
{
    float NoL = dot(normal, lightDir);
    float offsetScale = normalBias * (1.0 - NoL);
    return worldPos + normal * offsetScale;
}

// ============================================================================
// Shadow Comparison Functions
// ============================================================================

// Basic shadow depth comparison
float CompareShadowDepth(float shadowMapDepth, float fragmentDepth, float bias)
{
    return (fragmentDepth - bias <= shadowMapDepth) ? 1.0 : 0.0;
}

// Shadow comparison with smooth transition
float CompareShadowDepthSmooth(
    float shadowMapDepth,
    float fragmentDepth,
    float bias,
    float transitionScale)
{
    float diff = shadowMapDepth - (fragmentDepth - bias);
    return smoothstep(-transitionScale, transitionScale, diff);
}

// ============================================================================
// PCF Filtering Functions
// ============================================================================

// Poisson disk samples for high-quality PCF
const vec2 POISSON_DISK_16[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

// Rotated Poisson disk sampling for temporal stability
vec2 RotatePoissonSample(vec2 sample, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return vec2(
        sample.x * c - sample.y * s,
        sample.x * s + sample.y * c
    );
}

// ============================================================================
// PCF Filter Implementations
// ============================================================================

// 2x2 bilinear PCF
float PCF_2x2_Bilinear(
    sampler2D shadowMap,
    vec2 shadowUV,
    float compareDepth,
    vec2 texelSize,
    float bias)
{
    // Get the fractional part for bilinear interpolation
    vec2 f = fract(shadowUV * vec2(1.0 / texelSize.x, 1.0 / texelSize.y));
    
    // Sample 4 corners
    float s00 = CompareShadowDepth(texture(shadowMap, shadowUV).r, compareDepth, bias);
    float s10 = CompareShadowDepth(texture(shadowMap, shadowUV + vec2(texelSize.x, 0.0)).r, compareDepth, bias);
    float s01 = CompareShadowDepth(texture(shadowMap, shadowUV + vec2(0.0, texelSize.y)).r, compareDepth, bias);
    float s11 = CompareShadowDepth(texture(shadowMap, shadowUV + texelSize).r, compareDepth, bias);
    
    // Bilinear interpolation
    float s0 = mix(s00, s10, f.x);
    float s1 = mix(s01, s11, f.x);
    return mix(s0, s1, f.y);
}

// 3x3 PCF with optimized sampling
float PCF_3x3_Optimized(
    sampler2D shadowMap,
    vec2 shadowUV,
    float compareDepth,
    vec2 texelSize,
    float bias)
{
    float shadow = 0.0;
    
    // 9 samples in a 3x3 grid
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float depth = texture(shadowMap, shadowUV + offset).r;
            shadow += CompareShadowDepth(depth, compareDepth, bias);
        }
    }
    
    return shadow / 9.0;
}

// 5x5 PCF with Gaussian weights
float PCF_5x5_Gaussian(
    sampler2D shadowMap,
    vec2 shadowUV,
    float compareDepth,
    vec2 texelSize,
    float bias)
{
    // Gaussian kernel weights (sigma = 1.0)
    const float weights[25] = float[](
        0.003765, 0.015019, 0.023792, 0.015019, 0.003765,
        0.015019, 0.059912, 0.094907, 0.059912, 0.015019,
        0.023792, 0.094907, 0.150342, 0.094907, 0.023792,
        0.015019, 0.059912, 0.094907, 0.059912, 0.015019,
        0.003765, 0.015019, 0.023792, 0.015019, 0.003765
    );
    
    float shadow = 0.0;
    int index = 0;
    
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float depth = texture(shadowMap, shadowUV + offset).r;
            shadow += CompareShadowDepth(depth, compareDepth, bias) * weights[index];
            index++;
        }
    }
    
    return shadow;
}

// Poisson disk PCF (high quality, variable sample count)
float PCF_PoissonDisk(
    sampler2D shadowMap,
    vec2 shadowUV,
    float compareDepth,
    vec2 texelSize,
    float bias,
    float filterRadius,
    float rotationAngle,
    int sampleCount)
{
    float shadow = 0.0;
    
    for (int i = 0; i < sampleCount && i < 16; i++)
    {
        vec2 offset = RotatePoissonSample(POISSON_DISK_16[i], rotationAngle);
        offset *= filterRadius * texelSize;
        
        float depth = texture(shadowMap, shadowUV + offset).r;
        shadow += CompareShadowDepth(depth, compareDepth, bias);
    }
    
    return shadow / float(sampleCount);
}

// ============================================================================
// Point Light Shadow Functions
// ============================================================================

// Sample point light shadow from cube map
float SamplePointLightShadow(
    samplerCube shadowCubeMap,
    vec3 lightToFragment,
    float compareDepth,
    float bias)
{
    float shadowDepth = texture(shadowCubeMap, lightToFragment).r;
    return CompareShadowDepth(shadowDepth, compareDepth, bias);
}

// PCF for point light cube shadow maps
float PCF_PointLight(
    samplerCube shadowCubeMap,
    vec3 lightToFragment,
    float compareDepth,
    float bias,
    float filterRadius,
    int sampleCount)
{
    // Generate perpendicular vectors for sampling
    vec3 forward = normalize(lightToFragment);
    vec3 up = abs(forward.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, forward));
    up = cross(forward, right);
    
    float shadow = 0.0;
    float diskRadius = filterRadius;
    
    for (int i = 0; i < sampleCount && i < 16; i++)
    {
        vec2 offset = POISSON_DISK_16[i] * diskRadius;
        vec3 sampleDir = lightToFragment + right * offset.x + up * offset.y;
        
        float depth = texture(shadowCubeMap, sampleDir).r;
        shadow += CompareShadowDepth(depth, compareDepth, bias);
    }
    
    return shadow / float(sampleCount);
}

// ============================================================================
// Shadow Fade Functions
// ============================================================================

// Calculate shadow fade based on distance from camera
float CalculateShadowFade(
    float distance,
    float fadeStart,
    float fadeEnd)
{
    return 1.0 - clamp((distance - fadeStart) / (fadeEnd - fadeStart), 0.0, 1.0);
}

// Calculate cascade shadow fade for CSM
float CalculateCascadeFade(
    float cascadeDepth,
    float cascadeEnd,
    float fadeRange)
{
    float fadeStart = cascadeEnd - fadeRange;
    return 1.0 - clamp((cascadeDepth - fadeStart) / fadeRange, 0.0, 1.0);
}

// ============================================================================
// Utility Functions
// ============================================================================

// Convert depth from [0,1] to linear depth
float LinearizeDepth(float depth, float nearPlane, float farPlane)
{
    return nearPlane * farPlane / (farPlane - depth * (farPlane - nearPlane));
}

// Pack/unpack shadow depth for storage
float PackShadowDepth(float depth)
{
    return depth;
}

float UnpackShadowDepth(float packedDepth)
{
    return packedDepth;
}

// Calculate screen-space dithering for temporal anti-aliasing
float GetDitherValue(vec2 screenPos)
{
    // 4x4 Bayer matrix dithering
    int x = int(mod(screenPos.x, 4.0));
    int y = int(mod(screenPos.y, 4.0));
    
    const float bayerMatrix[16] = float[](
        0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
        3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    
    return bayerMatrix[y * 4 + x];
}

#endif // SHADOW_COMMON_GLSL
