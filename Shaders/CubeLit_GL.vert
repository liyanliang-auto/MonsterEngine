#version 330 core

// Vertex shader for lit cube rendering (OpenGL version)
// Supports multiple lights with Blinn-Phong shading

// Vertex inputs
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Transform uniform block (matches binding point 0)
layout(std140) uniform TransformUB
{
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 normalMatrix;
    vec4 cameraPosition;   // xyz = position, w = padding
    vec4 textureBlendVec;  // x = blend factor, yzw = padding
};

// Outputs to fragment shader
out vec3 fragWorldPos;
out vec3 fragNormal;
out vec2 fragTexCoord;
out vec3 fragViewPos;

void main() {
    // Transform position to world space
    vec4 worldPos = model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space using normal matrix
    fragNormal = mat3(normalMatrix) * inNormal;
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Camera position for specular calculations
    fragViewPos = cameraPosition.xyz;
    
    // Final clip space position
    gl_Position = projection * view * worldPos;
}
