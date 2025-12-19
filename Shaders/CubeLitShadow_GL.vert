// Copyright Monster Engine. All Rights Reserved.
// Cube Lit Vertex Shader with Shadow Support (OpenGL version)
// 
// Supports shadow mapping for directional lights.
// Reference: UE5 BasePassVertexShader.usf

#version 330 core

// ============================================================================
// Uniform Buffers
// ============================================================================

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 normalMatrix;
uniform vec4 cameraPosition;
uniform vec4 textureBlend;

// Shadow uniforms
uniform mat4 lightViewProjection;
uniform vec4 shadowParams;      // x = bias, y = slope bias, z = normal bias, w = shadow distance

// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// ============================================================================
// Vertex Output
// ============================================================================

out vec3 fragWorldPos;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec3 fragViewPos;
out vec4 fragShadowCoord;

// ============================================================================
// Main
// ============================================================================

void main() {
    // Transform to world space
    vec4 worldPos = model * vec4(inPosition, 1.0);
    
    // Transform to view space
    vec4 viewPos = view * worldPos;
    
    // Transform to clip space
    vec4 clipPos = projection * viewPos;
    
    // Transform normal to world space
    vec3 worldNormal = normalize(mat3(normalMatrix) * inNormal);
    
    // Apply normal offset bias for shadow mapping
    float normalBias = shadowParams.z;
    vec4 biasedWorldPos = worldPos + vec4(worldNormal * normalBias, 0.0);
    
    // Transform to light space for shadow mapping
    fragShadowCoord = lightViewProjection * biasedWorldPos;
    
    // Pass to fragment shader
    fragWorldPos = worldPos.xyz;
    fragNormal = worldNormal;
    fragTexCoord = inTexCoord;
    fragViewPos = cameraPosition.xyz;
    
    gl_Position = clipPos;
}
