#version 450 core

// ============================================================================
// TAA (Temporal Anti-Aliasing) Pass - Fragment Shader
// Implements temporal reprojection with variance clipping and optional sharpening
// ============================================================================

layout(location = 0) in vec2 vScreenUV;

// Input textures
layout(set = 0, binding = 0) uniform sampler2D currentColor;
layout(set = 0, binding = 1) uniform sampler2D motionVector;
layout(set = 0, binding = 2) uniform sampler2D historyColor;

// Scene UBO for TAA parameters
layout(set = 0, binding = 3) uniform SceneUBO {
    mat4 invViewProj;
    vec4 cameraPos;
    vec4 dirLightDirection;
    vec4 dirLightColorIntensity;
    vec4 pointLightPositionRadius;
    vec4 pointLightColorIntensity;
    vec4 ambient;
    mat4 previousViewProj;
    vec4 jitterOffset;
    vec4 taaParams;  // x=blendFactor, y=sharpness, z=enableSharpening, w=reserved
} scene;

layout(location = 0) out vec4 outColor;

// ============================================================================
// Catmull-Rom bicubic filtering for history sampling
// ============================================================================
vec3 sampleCatmullRom(sampler2D tex, vec2 uv, vec2 texelSize) {
    vec2 position = uv / texelSize;
    vec2 centerPosition = floor(position - 0.5) + 0.5;
    vec2 f = position - centerPosition;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;
    
    // Catmull-Rom weights
    vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    vec2 w3 = 0.5 * f3 - 0.5 * f2;
    
    vec2 s0 = w0 + w1;
    vec2 s1 = w2 + w3;
    vec2 f0 = w1 / s0;
    vec2 f1 = w3 / s1;
    
    vec2 t0 = centerPosition - 1.0 + f0;
    vec2 t1 = centerPosition + 1.0 + f1;
    
    return (texture(tex, t0 * texelSize).rgb * s0.x +
            texture(tex, vec2(t1.x, t0.y) * texelSize).rgb * s1.x) * s0.y +
           (texture(tex, vec2(t0.x, t1.y) * texelSize).rgb * s0.x +
            texture(tex, t1 * texelSize).rgb * s1.x) * s1.y;
}

// ============================================================================
// Variance clipping for history rejection
// ============================================================================
vec3 clipHistory(vec3 historyColor, vec3 currentColor, vec2 uv, vec2 texelSize) {
    // Sample 3x3 neighborhood
    vec3 c0 = texture(currentColor, uv + vec2(-1, -1) * texelSize).rgb;
    vec3 c1 = texture(currentColor, uv + vec2( 0, -1) * texelSize).rgb;
    vec3 c2 = texture(currentColor, uv + vec2( 1, -1) * texelSize).rgb;
    vec3 c3 = texture(currentColor, uv + vec2(-1,  0) * texelSize).rgb;
    vec3 c4 = currentColor;
    vec3 c5 = texture(currentColor, uv + vec2( 1,  0) * texelSize).rgb;
    vec3 c6 = texture(currentColor, uv + vec2(-1,  1) * texelSize).rgb;
    vec3 c7 = texture(currentColor, uv + vec2( 0,  1) * texelSize).rgb;
    vec3 c8 = texture(currentColor, uv + vec2( 1,  1) * texelSize).rgb;
    
    // Compute mean
    vec3 mean = (c0 + c1 + c2 + c3 + c4 + c5 + c6 + c7 + c8) / 9.0;
    
    // Compute variance
    vec3 m0 = c0 - mean;
    vec3 m1 = c1 - mean;
    vec3 m2 = c2 - mean;
    vec3 m3 = c3 - mean;
    vec3 m4 = c4 - mean;
    vec3 m5 = c5 - mean;
    vec3 m6 = c6 - mean;
    vec3 m7 = c7 - mean;
    vec3 m8 = c8 - mean;
    
    vec3 variance = (m0*m0 + m1*m1 + m2*m2 + m3*m3 + m4*m4 + 
                     m5*m5 + m6*m6 + m7*m7 + m8*m8) / 9.0;
    vec3 stdDev = sqrt(variance);
    
    // Clip history to mean ± 1.5 * stdDev
    vec3 colorMin = mean - stdDev * 1.5;
    vec3 colorMax = mean + stdDev * 1.5;
    
    return clamp(historyColor, colorMin, colorMax);
}

// ============================================================================
// Unsharp mask sharpening
// ============================================================================
vec3 sharpen(vec3 color, vec2 uv, vec2 texelSize, float sharpness) {
    vec3 c0 = texture(currentColor, uv).rgb;
    vec3 c1 = texture(currentColor, uv + vec2(-1,  0) * texelSize).rgb;
    vec3 c2 = texture(currentColor, uv + vec2( 1,  0) * texelSize).rgb;
    vec3 c3 = texture(currentColor, uv + vec2( 0, -1) * texelSize).rgb;
    vec3 c4 = texture(currentColor, uv + vec2( 0,  1) * texelSize).rgb;
    
    vec3 blurred = (c0 + c1 + c2 + c3 + c4) / 5.0;
    return color + (color - blurred) * sharpness;
}

// ============================================================================
// Main TAA pass
// ============================================================================
void main() {
    vec2 texelSize = 1.0 / textureSize(currentColor, 0);
    
    // Sample current frame color
    vec3 current = texture(currentColor, vScreenUV).rgb;
    
    // Sample motion vector
    vec2 motion = texture(motionVector, vScreenUV).rg;
    
    // Compute history UV by reprojecting using motion vector
    vec2 historyUV = vScreenUV - motion;
    
    // Check if history UV is valid (within screen bounds)
    bool validHistory = (historyUV.x >= 0.0 && historyUV.x <= 1.0 &&
                         historyUV.y >= 0.0 && historyUV.y <= 1.0);
    
    vec3 finalColor = current;
    
    if (validHistory) {
        // Sample history with Catmull-Rom filtering
        vec3 history = sampleCatmullRom(historyColor, historyUV, texelSize);
        
        // Clip history using variance clipping
        vec3 clippedHistory = clipHistory(history, current, vScreenUV, texelSize);
        
        // Blend current and history
        float blendFactor = scene.taaParams.x;
        finalColor = mix(clippedHistory, current, blendFactor);
        
        // Optional sharpening
        if (scene.taaParams.z > 0.5) {
            float sharpness = scene.taaParams.y;
            finalColor = sharpen(finalColor, vScreenUV, texelSize, sharpness);
        }
    }
    
    outColor = vec4(finalColor, 1.0);
}
