#version 450

layout(binding = 0) uniform TransformUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 normalMatrix;
    vec4 cameraPosition;
    vec4 textureBlend;
} transform;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragViewPos;

void main() {
    // Transform vertex to world space
    vec4 worldPos = transform.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space
    fragNormal = mat3(transform.normalMatrix) * inNormal;
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Camera position for view direction
    fragViewPos = transform.cameraPosition.xyz;
    
    // Final clip space position with Vulkan Y-axis flip
    vec4 clipPos = transform.projection * transform.view * worldPos;
    clipPos.y = -clipPos.y;  // Flip Y for Vulkan coordinate system
    gl_Position = clipPos;
}