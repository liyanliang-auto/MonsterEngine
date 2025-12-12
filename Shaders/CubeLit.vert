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
    // Use vertex position directly in clip space for debugging
    // Cube vertices are in range [-0.5, 0.5], scale up to be visible
    vec4 pos = vec4(inPosition * 0.5, 1.0);  // Scale down to fit in NDC
    
    // Apply MVP transform
    vec4 worldPos = transform.model * vec4(inPosition, 1.0);
    vec4 viewPos = transform.view * worldPos;
    vec4 clipPos = transform.projection * viewPos;
    
    // Pass to fragment shader
    fragWorldPos = worldPos.xyz;
    fragNormal = mat3(transform.normalMatrix) * inNormal;
    fragTexCoord = inTexCoord;
    fragViewPos = transform.cameraPosition.xyz;
    
    gl_Position = clipPos;
}