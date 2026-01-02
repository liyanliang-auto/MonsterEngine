// Copyright Monster Engine. All Rights Reserved.
// Shadow Projection Fragment Shader
// 
// Projects shadow depth maps onto the scene and computes shadow factors.
// Supports directional, point, and spot light shadows.
// Includes PCF soft shadow filtering.
//
// Reference: UE5 ShadowProjectionPixelShader.usf

#version 450

// ============================================================================
// Shadow Quality Levels
// ============================================================================
// 1 = No filtering (hard shadows)
// 2 = 2x2 PCF
// 3 = 3x3 PCF (default)
// 4 = 5x5 PCF
// 5 = 7x7 PCF (high quality)

#ifndef SHADOW_QUALITY
#define SHADOW_QUALITY 3
#endif

// Utility function - must be defined before use
float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

// ============================================================================
// Fragment Input
// ============================================================================

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inScreenPosition;

// ============================================================================
// Fragment Output
// ============================================================================

layout(location = 0) out vec4 outShadowMask;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniform buffer
layout(set = 0, binding = 0) uniform ViewData
{
    mat4 ViewMatrix;
    mat4 ProjectionMatrix;
    mat4 ViewProjectionMatrix;
    mat4 InvViewMatrix;
    mat4 InvProjectionMatrix;
    mat4 InvViewProjectionMatrix;
    vec4 ViewPosition;
    vec4 ViewForward;
    vec4 ScreenSize;            // xy = size, zw = 1/size
} View;

// Shadow projection uniform buffer
layout(set = 0, binding = 1) uniform ShadowProjectionData
{
    mat4 ScreenToShadowMatrix;  // Transforms screen position to shadow UV + depth
    vec4 ShadowUVMinMax;        // Shadow UV bounds for clamping
    vec4 ShadowBufferSize;      // xy = size, zw = 1/size
    vec4 ShadowParams;          // x = depth bias, y = slope bias, z = receiver bias, w = 1/max depth
    vec4 LightPositionAndType;  // xyz = position, w = type (0=dir, 1=point, 2=spot)
    vec4 LightDirectionAndRadius; // xyz = direction, w = radius
    vec4 ShadowFadeParams;      // x = fade start, y = 1/fade length, z = fade alpha, w = unused
    float TransitionScale;
    float SoftTransitionScale;
    float ProjectionDepthBias;
    float ShadowSharpen;
} ShadowProj;

// ============================================================================
// Textures
// ============================================================================

// Scene depth texture
layout(set = 1, binding = 0) uniform sampler2D SceneDepthTexture;

// Shadow depth texture
layout(set = 1, binding = 1) uniform sampler2D ShadowDepthTexture;

// Shadow comparison sampler (for hardware PCF)
layout(set = 1, binding = 2) uniform sampler2DShadow ShadowDepthSampler;

// ============================================================================
// Helper Functions
// ============================================================================

// Reconstruct world position from screen UV and depth
vec3 ReconstructWorldPosition(vec2 screenUV, float depth)
{
    // Convert to NDC
    vec4 ndc = vec4(screenUV * 2.0 - 1.0, depth, 1.0);
    
    // Transform to world space
    vec4 worldPos = View.InvViewProjectionMatrix * ndc;
    return worldPos.xyz / worldPos.w;
}

// Calculate shadow UV from world position
vec3 WorldToShadowUV(vec3 worldPos)
{
    vec4 shadowPos = ShadowProj.ScreenToShadowMatrix * vec4(worldPos, 1.0);
    return shadowPos.xyz / shadowPos.w;
}

// Clamp shadow UV to valid range
vec2 ClampShadowUV(vec2 shadowUV)
{
    return clamp(shadowUV, ShadowProj.ShadowUVMinMax.xy, ShadowProj.ShadowUVMinMax.zw);
}

// ============================================================================
// PCF Shadow Filtering
// ============================================================================

// Simple shadow comparison
float ShadowCompare(vec2 shadowUV, float compareDepth)
{
    float shadowDepth = texture(ShadowDepthTexture, shadowUV).r;
    return (compareDepth <= shadowDepth + ShadowProj.ShadowParams.x) ? 1.0 : 0.0;
}

// Hardware PCF using comparison sampler
float ShadowComparePCF(vec3 shadowCoord)
{
    return texture(ShadowDepthSampler, shadowCoord);
}

// 2x2 PCF filter
float PCF2x2(vec2 shadowUV, float compareDepth)
{
    vec2 texelSize = ShadowProj.ShadowBufferSize.zw;
    
    float shadow = 0.0;
    shadow += ShadowCompare(shadowUV + vec2(-0.5, -0.5) * texelSize, compareDepth);
    shadow += ShadowCompare(shadowUV + vec2( 0.5, -0.5) * texelSize, compareDepth);
    shadow += ShadowCompare(shadowUV + vec2(-0.5,  0.5) * texelSize, compareDepth);
    shadow += ShadowCompare(shadowUV + vec2( 0.5,  0.5) * texelSize, compareDepth);
    
    return shadow * 0.25;
}

// 3x3 PCF filter
float PCF3x3(vec2 shadowUV, float compareDepth)
{
    vec2 texelSize = ShadowProj.ShadowBufferSize.zw;
    
    float shadow = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            shadow += ShadowCompare(shadowUV + offset, compareDepth);
        }
    }
    
    return shadow / 9.0;
}

// 5x5 PCF filter
float PCF5x5(vec2 shadowUV, float compareDepth)
{
    vec2 texelSize = ShadowProj.ShadowBufferSize.zw;
    
    // Optimized 5x5 using bilinear filtering
    // Sample at 9 locations with bilinear weights
    const float weights[9] = float[](
        1.0, 2.0, 1.0,
        2.0, 4.0, 2.0,
        1.0, 2.0, 1.0
    );
    
    float shadow = 0.0;
    float totalWeight = 0.0;
    
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float weight = 1.0;
            shadow += ShadowCompare(shadowUV + offset, compareDepth) * weight;
            totalWeight += weight;
        }
    }
    
    return shadow / totalWeight;
}

// 7x7 PCF filter (high quality)
float PCF7x7(vec2 shadowUV, float compareDepth)
{
    vec2 texelSize = ShadowProj.ShadowBufferSize.zw;
    
    float shadow = 0.0;
    float totalWeight = 0.0;
    
    // Use Gaussian-like weights for smoother falloff
    for (int y = -3; y <= 3; y++)
    {
        for (int x = -3; x <= 3; x++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            // Simple distance-based weight
            float dist = length(vec2(float(x), float(y)));
            float weight = 1.0 / (1.0 + dist * 0.5);
            shadow += ShadowCompare(shadowUV + offset, compareDepth) * weight;
            totalWeight += weight;
        }
    }
    
    return shadow / totalWeight;
}

// Select PCF filter based on quality level
float FilterShadow(vec2 shadowUV, float compareDepth)
{
#if SHADOW_QUALITY == 1
    return ShadowCompare(shadowUV, compareDepth);
#elif SHADOW_QUALITY == 2
    return PCF2x2(shadowUV, compareDepth);
#elif SHADOW_QUALITY == 3
    return PCF3x3(shadowUV, compareDepth);
#elif SHADOW_QUALITY == 4
    return PCF5x5(shadowUV, compareDepth);
#else
    return PCF7x7(shadowUV, compareDepth);
#endif
}

// ============================================================================
// Shadow Fade
// ============================================================================

float CalculateShadowFade(float distance)
{
    float fadeStart = ShadowProj.ShadowFadeParams.x;
    float invFadeLength = ShadowProj.ShadowFadeParams.y;
    
    float fade = saturate((distance - fadeStart) * invFadeLength);
    return 1.0 - fade;
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    // Sample scene depth
    float sceneDepth = texture(SceneDepthTexture, inTexCoord).r;
    
    // Early out for sky pixels (depth = 1.0)
    if (sceneDepth >= 0.9999)
    {
        outShadowMask = vec4(1.0);
        return;
    }
    
    // Reconstruct world position
    vec3 worldPos = ReconstructWorldPosition(inTexCoord, sceneDepth);
    
    // Calculate distance from camera for fade
    float distanceFromCamera = length(worldPos - View.ViewPosition.xyz);
    
    // Calculate shadow UV and depth
    vec3 shadowCoord = WorldToShadowUV(worldPos);
    vec2 shadowUV = ClampShadowUV(shadowCoord.xy);
    float compareDepth = shadowCoord.z;
    
    // Check if pixel is outside shadow map bounds
    if (shadowCoord.x < ShadowProj.ShadowUVMinMax.x || 
        shadowCoord.x > ShadowProj.ShadowUVMinMax.z ||
        shadowCoord.y < ShadowProj.ShadowUVMinMax.y || 
        shadowCoord.y > ShadowProj.ShadowUVMinMax.w ||
        compareDepth < 0.0 || compareDepth > 1.0)
    {
        outShadowMask = vec4(1.0);
        return;
    }
    
    // Apply receiver bias
    compareDepth -= ShadowProj.ProjectionDepthBias;
    
    // Calculate shadow factor with PCF filtering
    float shadow = FilterShadow(shadowUV, compareDepth);
    
    // Apply shadow sharpening
    if (ShadowProj.ShadowSharpen > 0.0)
    {
        shadow = saturate((shadow - 0.5) * (1.0 + ShadowProj.ShadowSharpen) + 0.5);
    }
    
    // Apply distance fade
    float fadeFactor = CalculateShadowFade(distanceFromCamera);
    shadow = mix(1.0, shadow, fadeFactor);
    
    // Apply fade alpha
    shadow = mix(1.0, shadow, ShadowProj.ShadowFadeParams.z);
    
    // Output shadow mask
    outShadowMask = vec4(shadow, shadow, shadow, 1.0);
}

