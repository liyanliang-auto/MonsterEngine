// Copyright Monster Engine. All Rights Reserved.
// Shadow Projection Fragment Shader - OpenGL Version
// 
// Projects shadow depth maps onto the scene and computes shadow factors.
// Supports directional, point, and spot light shadows.
// Includes PCF soft shadow filtering.
//
// Reference: UE5 ShadowProjectionPixelShader.usf

#version 330 core

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

// ============================================================================
// Fragment Input
// ============================================================================

in vec2 TexCoord;
in vec4 ScreenPosition;

// ============================================================================
// Fragment Output
// ============================================================================

out vec4 FragColor;

// ============================================================================
// Uniform Buffers
// ============================================================================

// View uniforms
uniform mat4 ViewMatrix;
uniform mat4 ProjectionMatrix;
uniform mat4 ViewProjectionMatrix;
uniform mat4 InvViewMatrix;
uniform mat4 InvProjectionMatrix;
uniform mat4 InvViewProjectionMatrix;
uniform vec4 ViewPosition;
uniform vec4 ScreenSize;  // xy = size, zw = 1/size

// Shadow projection uniforms
uniform mat4 ScreenToShadowMatrix;
uniform vec4 ShadowUVMinMax;
uniform vec4 ShadowBufferSize;  // xy = size, zw = 1/size
uniform vec4 ShadowParams;      // x = depth bias, y = slope bias, z = receiver bias, w = 1/max depth
uniform vec4 LightPositionAndType;
uniform vec4 LightDirectionAndRadius;
uniform vec4 ShadowFadeParams;
uniform float TransitionScale;
uniform float SoftTransitionScale;
uniform float ProjectionDepthBias;
uniform float ShadowSharpen;

// ============================================================================
// Textures
// ============================================================================

uniform sampler2D SceneDepthTexture;
uniform sampler2D ShadowDepthTexture;
uniform sampler2DShadow ShadowDepthSampler;

// ============================================================================
// Helper Functions
// ============================================================================

float saturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 ReconstructWorldPosition(vec2 screenUV, float depth)
{
    vec4 ndc = vec4(screenUV * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 worldPos = InvViewProjectionMatrix * ndc;
    return worldPos.xyz / worldPos.w;
}

vec3 WorldToShadowUV(vec3 worldPos)
{
    vec4 shadowPos = ScreenToShadowMatrix * vec4(worldPos, 1.0);
    return shadowPos.xyz / shadowPos.w;
}

vec2 ClampShadowUV(vec2 shadowUV)
{
    return clamp(shadowUV, ShadowUVMinMax.xy, ShadowUVMinMax.zw);
}

// ============================================================================
// PCF Shadow Filtering
// ============================================================================

float ShadowCompare(vec2 shadowUV, float compareDepth)
{
    float shadowDepth = texture(ShadowDepthTexture, shadowUV).r;
    return (compareDepth <= shadowDepth + ShadowParams.x) ? 1.0 : 0.0;
}

float PCF2x2(vec2 shadowUV, float compareDepth)
{
    vec2 texelSize = ShadowBufferSize.zw;
    
    float shadow = 0.0;
    shadow += ShadowCompare(shadowUV + vec2(-0.5, -0.5) * texelSize, compareDepth);
    shadow += ShadowCompare(shadowUV + vec2( 0.5, -0.5) * texelSize, compareDepth);
    shadow += ShadowCompare(shadowUV + vec2(-0.5,  0.5) * texelSize, compareDepth);
    shadow += ShadowCompare(shadowUV + vec2( 0.5,  0.5) * texelSize, compareDepth);
    
    return shadow * 0.25;
}

float PCF3x3(vec2 shadowUV, float compareDepth)
{
    vec2 texelSize = ShadowBufferSize.zw;
    
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

float PCF5x5(vec2 shadowUV, float compareDepth)
{
    vec2 texelSize = ShadowBufferSize.zw;
    
    float shadow = 0.0;
    float totalWeight = 0.0;
    
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            shadow += ShadowCompare(shadowUV + offset, compareDepth);
            totalWeight += 1.0;
        }
    }
    
    return shadow / totalWeight;
}

float FilterShadow(vec2 shadowUV, float compareDepth)
{
#if SHADOW_QUALITY == 1
    return ShadowCompare(shadowUV, compareDepth);
#elif SHADOW_QUALITY == 2
    return PCF2x2(shadowUV, compareDepth);
#elif SHADOW_QUALITY == 3
    return PCF3x3(shadowUV, compareDepth);
#else
    return PCF5x5(shadowUV, compareDepth);
#endif
}

float CalculateShadowFade(float distance)
{
    float fadeStart = ShadowFadeParams.x;
    float invFadeLength = ShadowFadeParams.y;
    
    float fade = saturate((distance - fadeStart) * invFadeLength);
    return 1.0 - fade;
}

// ============================================================================
// Main
// ============================================================================

void main()
{
    float sceneDepth = texture(SceneDepthTexture, TexCoord).r;
    
    if (sceneDepth >= 0.9999)
    {
        FragColor = vec4(1.0);
        return;
    }
    
    vec3 worldPos = ReconstructWorldPosition(TexCoord, sceneDepth);
    float distanceFromCamera = length(worldPos - ViewPosition.xyz);
    
    vec3 shadowCoord = WorldToShadowUV(worldPos);
    vec2 shadowUV = ClampShadowUV(shadowCoord.xy);
    float compareDepth = shadowCoord.z;
    
    if (shadowCoord.x < ShadowUVMinMax.x || 
        shadowCoord.x > ShadowUVMinMax.z ||
        shadowCoord.y < ShadowUVMinMax.y || 
        shadowCoord.y > ShadowUVMinMax.w ||
        compareDepth < 0.0 || compareDepth > 1.0)
    {
        FragColor = vec4(1.0);
        return;
    }
    
    compareDepth -= ProjectionDepthBias;
    
    float shadow = FilterShadow(shadowUV, compareDepth);
    
    if (ShadowSharpen > 0.0)
    {
        shadow = saturate((shadow - 0.5) * (1.0 + ShadowSharpen) + 0.5);
    }
    
    float fadeFactor = CalculateShadowFade(distanceFromCamera);
    shadow = mix(1.0, shadow, fadeFactor);
    shadow = mix(1.0, shadow, ShadowFadeParams.z);
    
    FragColor = vec4(shadow, shadow, shadow, 1.0);
}
