#version 330 core

// ImGui Vertex Shader for OpenGL
// Transforms 2D UI vertices and passes through UV and color

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

out vec2 fragTexCoord;
out vec4 fragColor;

uniform mat4 projectionMatrix;

void main() {
    fragTexCoord = inTexCoord;
    fragColor = inColor;
    gl_Position = projectionMatrix * vec4(inPosition, 0.0, 1.0);
}
