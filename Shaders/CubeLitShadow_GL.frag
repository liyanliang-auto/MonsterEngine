// Copyright Monster Engine. All Rights Reserved.
// Cube Lit Fragment Shader with Shadow Support (OpenGL version)
// 
// Supports shadow mapping with PCF soft shadows.
// Reference: UE5 BasePassPixelShader.usf, ShadowFilteringCommon.ush

#version 330 core

// ============================================================================
// Uniforms
// ============================================================================

uniform vec4 cameraPosition;
uniform vec4 textureBlend;

// Light data structure (max 8 lights)
uniform vec4 lightPositions[8];
uniform vec4 lightColors[8];
uniform vec4 lightParams[8];
uniform vec4 ambientColor;
uniform int numLights;

// Shadow uniforms
uniform vec4 shadowParams;      // x = bias, y = slope bias, z = normal bias, w = shadow distance
uniform vec4 shadowMapSize;     // xy = size, zw = 1/size

// ============================================================================
// Textures
// ============================================================================

uniform sampler2D texture1;
uniform sampler2D texture2;
uniform sampler2D shadowMap;

// ============================================================================
// Fragment Input
// ============================================================================

in vec3 fragWorldPos;
in vec3 fragNormal;
in vec2 fragTexCoord;
in vec3 fragViewPos;
in vec4 fragShadowCoord;

// ============================================================================
// Fragment Output
// ============================================================================

out vec4 outColor;

// ============================================================================
// Shadow Sampling Functions
// ============================================================================

float shadowCompare(vec2 uv, float compareDepth) {
    float shadowDepth = texture(shadowMap, uv).r;
    float bias = shadowParams.x;
    return (compareDepth - bias <= shadowDepth) ? 1.0 : 0.0;
}

float pcf3x3(vec2 uv, float compareDepth) {
    vec2 texelSize = shadowMapSize.zw;
    float result = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += shadowCompare(uv + offset, compareDepth);
        }
    }
    return result / 9.0;
}

float calculateShadow(vec4 shadowCoord) {
    // Perspective divide
    vec3 projCoords = shadowCoord.xyz / shadowCoord.w;
    
    // Transform to [0,1] range
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    
    // Check bounds
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;
    }
    
    // Distance fade
    float distanceFromCamera = length(fragWorldPos - fragViewPos);
    float shadowDistance = shadowParams.w;
    if (distanceFromCamera > shadowDistance) {
        return 1.0;
    }
    
    return pcf3x3(projCoords.xy, projCoords.z);
}

// ============================================================================
// Lighting Functions
// ============================================================================

vec3 calculateLight(int lightIndex, vec3 normal, vec3 viewDir, vec3 baseColor, float shadowFactor) {
    vec4 lightPos = lightPositions[lightIndex];
    vec4 lightColor = lightColors[lightIndex];
    vec4 lightParam = lightParams[lightIndex];
    
    vec3 lightDir;
    float attenuation = 1.0;
    float lightShadow = 1.0;
    
    if (lightPos.w < 0.5) {
        // Directional light
        lightDir = normalize(-lightPos.xyz);
        if (lightIndex == 0) {
            lightShadow = shadowFactor;
        }
    } else {
        // Point/Spot light
        vec3 lightVec = lightPos.xyz - fragWorldPos;
        float distance = length(lightVec);
        lightDir = normalize(lightVec);
        
        float radius = lightParam.x;
        if (radius > 0.0) {
            attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
            attenuation *= attenuation;
        }
    }
    
    float intensity = lightColor.a;
    
    // Diffuse
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = NdotL * lightColor.rgb * intensity;
    
    // Specular
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfwayDir), 0.0);
    float spec = pow(NdotH, 32.0);
    vec3 specular = spec * lightColor.rgb * intensity * 0.5;
    
    return (diffuse + specular) * attenuation * lightShadow;
}

// ============================================================================
// Main
// ============================================================================

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(fragViewPos - fragWorldPos);
    
    // Sample textures
    vec4 texColor1 = texture(texture1, fragTexCoord);
    vec4 texColor2 = texture(texture2, fragTexCoord);
    
    float blendFactor = textureBlend.x;
    vec3 baseColor = mix(texColor1.rgb, texColor2.rgb, blendFactor);
    
    if (texColor1.a < 0.01 && texColor2.a < 0.01) {
        baseColor = vec3(0.8, 0.6, 0.4);
    }
    
    // Calculate shadow
    float shadowFactor = calculateShadow(fragShadowCoord);
    
    // Ambient
    vec3 ambient = ambientColor.rgb * ambientColor.a * baseColor;
    vec3 finalColor = ambient;
    
    // Add lights
    int lightCount = min(numLights, 8);
    for (int i = 0; i < lightCount; i++) {
        finalColor += calculateLight(i, normal, viewDir, baseColor, shadowFactor) * baseColor;
    }
    
    if (lightCount == 0) {
        finalColor = baseColor * 0.3;
    }
    
    outColor = vec4(finalColor, 1.0);
}
