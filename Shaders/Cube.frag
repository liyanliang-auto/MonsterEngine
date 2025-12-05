#version 450
// Cube Fragment Shader
// Reference: LearnOpenGL Coordinate Systems tutorial

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;

// Output color
layout(location = 0) out vec4 outColor;

// Texture samplers - explicitly use set=0 to match descriptor layout
layout(set = 0, binding = 1) uniform sampler2D texture1;  // Container texture
layout(set = 0, binding = 2) uniform sampler2D texture2;  // Awesomeface texture

void main() {
    // Sample both textures
    vec4 color1 = texture(texture1, fragTexCoord);
    vec4 color2 = texture(texture2, fragTexCoord);
    
    // Mix textures: 80% container + 20% awesomeface
    outColor = mix(color1, color2, 0.2);
}
