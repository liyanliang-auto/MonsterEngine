#version 460 core

/**
 * Cube_GL.frag
 * 
 * OpenGL 4.6 fragment shader for textured cube rendering
 * Reference: LearnOpenGL Coordinate Systems tutorial
 */

// Input from vertex shader
layout(location = 0) in vec2 fragTexCoord;

// Texture samplers
layout(binding = 1) uniform sampler2D texture1;  // container texture
layout(binding = 2) uniform sampler2D texture2;  // awesomeface texture

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Sample both textures
    vec4 texColor1 = texture(texture1, fragTexCoord);
    vec4 texColor2 = texture(texture2, fragTexCoord);
    
    // Mix the two textures (80% container, 20% awesomeface)
    outColor = mix(texColor1, texColor2, 0.2);
}
