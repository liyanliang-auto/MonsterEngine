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
layout(location = 4) flat out int vertexID;

void main() {
    // UE5 row-vector convention: v * M (position on left, matrix on right)
    vec4 worldPos = vec4(inPosition, 1.0) * transform.model;
    vec4 viewPos = worldPos * transform.view;
    vec4 clipPos = viewPos * transform.projection;

    // Pass to fragment shader
    fragWorldPos = worldPos.xyz;
    fragNormal = inNormal * mat3(transform.normalMatrix);
    fragTexCoord = inTexCoord;
    fragViewPos = transform.cameraPosition.xyz;
    vertexID = gl_VertexIndex;

    gl_Position = clipPos;
}