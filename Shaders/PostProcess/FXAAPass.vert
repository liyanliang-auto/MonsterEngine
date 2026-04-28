#version 450

/**
 * FXAA Fullscreen Triangle Vertex Shader
 * 
 * Generates a fullscreen triangle without vertex buffer.
 * Reference: TAAPass.vert, UE5 ScreenPass.ush
 * 
 * Triangle vertices:
 *   Vertex 0: (-1, -1, 0) UV (0, 0)
 *   Vertex 1: ( 3, -1, 0) UV (2, 0)
 *   Vertex 2: (-1,  3, 0) UV (0, 2)
 * 
 * This covers the entire screen with a single triangle.
 */

layout(location = 0) out vec2 outUV;

void main()
{
    // Generate fullscreen triangle using vertex index
    // gl_VertexIndex: 0, 1, 2
    // outUV calculation:
    //   Index 0: (0 << 1) & 2 = 0, 0 & 2 = 0 → UV (0, 0)
    //   Index 1: (1 << 1) & 2 = 2, 1 & 2 = 0 → UV (1, 0)
    //   Index 2: (2 << 1) & 2 = 0, 2 & 2 = 2 → UV (0, 1)
    // But we want (2, 0) and (0, 2) for oversized triangle
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    
    // Convert UV [0,2] to NDC [-1,3]
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
