// Copyright Monster Engine. All Rights Reserved.
// Cube Lit Fragment Shader with Shadow Support (Vulkan version)
// 
// Supports shadow mapping with PCF soft shadows.
// Reference: UE5 BasePassPixelShader.usf, ShadowFilteringCommon.ush

#version 450

// ============================================================================
// Shadow Quality Configuration
// ============================================================================
// 1 = Hard shadows (no filtering)
// 2 = 2x2 PCF
// 3 = 3x3 PCF (default)
// 4 = 5x5 PCF

#ifndef SHADOW_QUALITY
#define SHADOW_QUALITY 3
#endif

// ============================================================================
// Uniform Buffers
// ============================================================================

layout(set = 0, binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 normalMatrix;
    vec4 cameraPosition;
    vec4 textureBlend;
} transform;

// Textures moved to set 0 for compatibility with single descriptor set binding
layout(set = 0, binding = 1) uniform sampler2D texture1;
layout(set = 0, binding = 2) uniform sampler2D texture2;

struct LightData {
    vec4 position;    // xyz = position/direction, w = type (0=directional, 1=point/spot)
    vec4 color;       // rgb = color, a = intensity
    vec4 params;      // x = radius, y = source radius, z/w = reserved
};

layout(set = 0, binding = 3) uniform LightingUBO {
    LightData lights[8];
    vec4 ambientColor;
    int numLights;
    float padding1;
    float padding2;
    float padding3;
} lighting;

layout(set = 0, binding = 4) uniform ShadowUBO {
    mat4 lightViewProjection;
    vec4 shadowParams;      // x = bias, y = slope bias, z = normal bias, w = shadow distance
    vec4 shadowMapSize;     // xy = size, zw = 1/size
} shadow;

layout(set = 0, binding = 5) uniform sampler2D shadowMap;
layout(set = 0, binding = 6) uniform sampler2D diffuseTexture;

// ============================================================================
// Fragment Input
// ============================================================================

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragViewPos;
layout(location = 4) in vec4 fragShadowCoord;
layout(location = 5) flat in int vertexID;

// ============================================================================
// Fragment Output
// ============================================================================

layout(location = 0) out vec4 outColor;

// ============================================================================
// Shadow Sampling Functions
// ============================================================================

// Basic shadow comparison
float shadowCompare(vec2 uv, float compareDepth) {
    float shadowDepth = texture(shadowMap, uv).r;
    float bias = shadow.shadowParams.x;
    return (compareDepth - bias <= shadowDepth) ? 1.0 : 0.0;
}

// 2x2 PCF
float pcf2x2(vec2 uv, float compareDepth) {
    vec2 texelSize = shadow.shadowMapSize.zw;
    float result = 0.0;
    result += shadowCompare(uv + vec2(-0.5, -0.5) * texelSize, compareDepth);
    result += shadowCompare(uv + vec2( 0.5, -0.5) * texelSize, compareDepth);
    result += shadowCompare(uv + vec2(-0.5,  0.5) * texelSize, compareDepth);
    result += shadowCompare(uv + vec2( 0.5,  0.5) * texelSize, compareDepth);
    return result * 0.25;
}

// 3x3 PCF
float pcf3x3(vec2 uv, float compareDepth) {
    vec2 texelSize = shadow.shadowMapSize.zw;
    float result = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += shadowCompare(uv + offset, compareDepth);
        }
    }
    return result / 9.0;
}

// 5x5 PCF
float pcf5x5(vec2 uv, float compareDepth) {
    vec2 texelSize = shadow.shadowMapSize.zw;
    float result = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += shadowCompare(uv + offset, compareDepth);
        }
    }
    return result / 25.0;
}

// Calculate shadow factor
float calculateShadow(vec4 shadowCoord) {
    // Perspective divide
    vec3 projCoords = shadowCoord.xyz / shadowCoord.w;
    
    // Transform to [0,1] range
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    // Check if outside shadow map bounds
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;  // No shadow outside bounds
    }
    
    // Apply shadow distance fade
    float distanceFromCamera = length(fragWorldPos - fragViewPos);
    float shadowDistance = shadow.shadowParams.w;
    if (distanceFromCamera > shadowDistance) {
        return 1.0;  // No shadow beyond shadow distance
    }
    
    // Calculate shadow with PCF
    float compareDepth = projCoords.z;
    
#if SHADOW_QUALITY == 1
    return shadowCompare(projCoords.xy, compareDepth);
#elif SHADOW_QUALITY == 2
    return pcf2x2(projCoords.xy, compareDepth);
#elif SHADOW_QUALITY == 3
    return pcf3x3(projCoords.xy, compareDepth);
#else
    return pcf5x5(projCoords.xy, compareDepth);
#endif
}

// ============================================================================
// Lighting Functions
// ============================================================================

vec3 calculateLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 baseColor, float shadowFactor) {
    LightData light = lighting.lights[lightIndex];
    
    vec3 lightDir;
    float attenuation = 1.0;
    float lightShadow = 1.0;
    
    // Determine light direction and attenuation based on light type
    if (light.position.w < 0.5) {
        // Directional light - position is actually direction
        lightDir = normalize(-light.position.xyz);
        // Apply shadow only to directional light (index 0)
        if (lightIndex == 0) {
            lightShadow = shadowFactor;
        }
    } else {
        // Point/Spot light
        vec3 lightVec = light.position.xyz - fragWorldPos;
        float distance = length(lightVec);
        lightDir = normalize(lightVec);
        
        // Distance attenuation
        float radius = light.params.x;
        if (radius > 0.0) {
            attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
            attenuation *= attenuation;
        }
    }
    
    vec3 lightColor = light.color.rgb;
    float intensity = light.color.a;
    
    // Diffuse (Lambertian)
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = NdotL * lightColor * intensity;
    
    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfwayDir), 0.0);
    float specPower = 32.0;
    float spec = pow(NdotH, specPower);
    vec3 specular = spec * lightColor * intensity * 0.5;
    
    return (diffuse + specular) * attenuation * lightShadow;
}

// ============================================================================
// Main
// ============================================================================

void main() {
    // Normalize interpolated normal
    vec3 normal = normalize(fragNormal);
    
    // View direction (from fragment to camera)
    vec3 viewDir = normalize(fragViewPos - fragWorldPos);
    
    // Sample diffuse texture for floor
    vec4 diffuseColor = texture(diffuseTexture, fragTexCoord);
    
    // Sample cube textures
    vec4 texColor1 = texture(texture1, fragTexCoord);
    vec4 texColor2 = texture(texture2, fragTexCoord);
    
    // Determine base color based on which texture is valid
    vec3 baseColor;
    
    // If diffuse texture has valid alpha, use it (floor)
    if (diffuseColor.a > 0.01) {
        baseColor = diffuseColor.rgb;
    }
    // Otherwise blend cube textures
    else if (texColor1.a > 0.01 || texColor2.a > 0.01) {
        float blendFactor = transform.textureBlend.x;
        baseColor = mix(texColor1.rgb, texColor2.rgb, blendFactor);
    }
    // Default color if no texture
    else {
        baseColor = vec3(0.8, 0.6, 0.4);
    }
    
    // Calculate shadow factor
    float shadowFactor = calculateShadow(fragShadowCoord);
    
    // Start with ambient lighting
    vec3 ambient = lighting.ambientColor.rgb * lighting.ambientColor.a * baseColor;
    vec3 finalColor = ambient;
    
    // Add contribution from each light
    int lightCount = min(lighting.numLights, 8);
    for (int i = 0; i < lightCount; i++) {
        finalColor += calculateLight(i, normal, viewDir, baseColor, shadowFactor) * baseColor;
    }
    
    // If no lights, use base color with some ambient
    if (lightCount == 0) {
        finalColor = baseColor * 0.3;
    }
    
    // Output final color
    outColor = vec4(finalColor, 1.0);
}
