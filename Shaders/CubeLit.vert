#version 450

// Vertex shader for lit cube rendering
// Supports multiple lights with Blinn-Phong shading

// Transform uniform buffer (binding 0)
layout(binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 normalMatrix;
    vec4 cameraPosition;
    vec4 textureBlend;
} transform;

// Vertex inputs
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragViewPos;

void main() {
    // Transform position to world space
    vec4 worldPos = transform.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space using normal matrix
    fragNormal = mat3(transform.normalMatrix) * inNormal;
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Camera position for specular calculations
    fragViewPos = transform.cameraPosition.xyz;
    
    // Final clip space position
    gl_Position = transform.projection * transform.view * worldPos;
}
