// Copyright Monster Engine. All Rights Reserved.
// Cube Lit Vertex Shader with Shadow Support (Vulkan version)
// 
// Supports shadow mapping for directional lights.
// Reference: UE5 BasePassVertexShader.usf

#version 450

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

// Shadow matrix uniform buffer
layout(set = 0, binding = 4) uniform ShadowUBO {
    mat4 lightViewProjection;   // Light space VP matrix
    vec4 shadowParams;          // x = bias, y = slope bias, z = normal bias, w = shadow distance
    vec4 shadowMapSize;         // xy = size, zw = 1/size
} shadow;

// ============================================================================
// Vertex Input
// ============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// ============================================================================
// Vertex Output
// ============================================================================

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragViewPos;
layout(location = 4) out vec4 fragShadowCoord;  // Shadow map coordinates
layout(location = 5) flat out int vertexID;

// ============================================================================
// Main
// ============================================================================

void main() {
    // Transform to world space
    vec4 worldPos = transform.model * vec4(inPosition, 1.0);
    
    // Transform to view space
    vec4 viewPos = transform.view * worldPos;
    
    // Transform to clip space
    vec4 clipPos = transform.projection * viewPos;
    
    // Transform to light space for shadow mapping
    vec4 shadowPos = shadow.lightViewProjection * worldPos;
    
    // Apply normal offset bias to reduce shadow acne
    vec3 worldNormal = normalize(mat3(transform.normalMatrix) * inNormal);
    float normalBias = shadow.shadowParams.z;
    vec4 biasedWorldPos = worldPos + vec4(worldNormal * normalBias, 0.0);
    vec4 biasedShadowPos = shadow.lightViewProjection * biasedWorldPos;
    
    // Pass to fragment shader
    fragWorldPos = worldPos.xyz;
    fragNormal = worldNormal;
    fragTexCoord = inTexCoord;
    fragViewPos = transform.cameraPosition.xyz;
    fragShadowCoord = biasedShadowPos;
    vertexID = gl_VertexIndex;
    
    gl_Position = clipPos;
}
