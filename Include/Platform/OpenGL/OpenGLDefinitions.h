#pragma once

/**
 * OpenGLDefinitions.h
 * 
 * OpenGL 4.6 Core Profile definitions and constants for MonsterEngine RHI
 * Reference: UE5 OpenGLDrv/Public/OpenGL.h
 * 
 * This file contains:
 * - OpenGL type definitions
 * - OpenGL constants and enums
 * - Platform-specific includes
 */

#include "Core/CoreMinimal.h"

// Platform-specific OpenGL headers
#if PLATFORM_WINDOWS
    // Prevent Windows.h from defining min/max macros
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
#endif

// OpenGL type definitions (matching GL/gl.h)
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef struct __GLsync* GLsync;

// OpenGL debug callback type
typedef void (APIENTRY* GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, 
                                      GLsizei length, const GLchar* message, const void* userParam);

namespace MonsterEngine::OpenGL {

// ============================================================================
// OpenGL Constants
// ============================================================================

// Boolean values
constexpr GLboolean GL_FALSE = 0;
constexpr GLboolean GL_TRUE = 1;

// Data types
constexpr GLenum GL_BYTE = 0x1400;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_SHORT = 0x1402;
constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
constexpr GLenum GL_INT = 0x1404;
constexpr GLenum GL_UNSIGNED_INT = 0x1405;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_DOUBLE = 0x140A;
constexpr GLenum GL_HALF_FLOAT = 0x140B;

// Primitive types
constexpr GLenum GL_POINTS = 0x0000;
constexpr GLenum GL_LINES = 0x0001;
constexpr GLenum GL_LINE_LOOP = 0x0002;
constexpr GLenum GL_LINE_STRIP = 0x0003;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_TRIANGLE_STRIP = 0x0005;
constexpr GLenum GL_TRIANGLE_FAN = 0x0006;
constexpr GLenum GL_PATCHES = 0x000E;

// Buffer targets
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_UNIFORM_BUFFER = 0x8A11;
constexpr GLenum GL_SHADER_STORAGE_BUFFER = 0x90D2;
constexpr GLenum GL_PIXEL_PACK_BUFFER = 0x88EB;
constexpr GLenum GL_PIXEL_UNPACK_BUFFER = 0x88EC;
constexpr GLenum GL_COPY_READ_BUFFER = 0x8F36;
constexpr GLenum GL_COPY_WRITE_BUFFER = 0x8F37;
constexpr GLenum GL_DRAW_INDIRECT_BUFFER = 0x8F3F;
constexpr GLenum GL_DISPATCH_INDIRECT_BUFFER = 0x90EE;

// Buffer usage
constexpr GLenum GL_STREAM_DRAW = 0x88E0;
constexpr GLenum GL_STREAM_READ = 0x88E1;
constexpr GLenum GL_STREAM_COPY = 0x88E2;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_STATIC_READ = 0x88E5;
constexpr GLenum GL_STATIC_COPY = 0x88E6;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_DYNAMIC_READ = 0x88E9;
constexpr GLenum GL_DYNAMIC_COPY = 0x88EA;

// Buffer storage flags (GL 4.4+)
constexpr GLbitfield GL_MAP_READ_BIT = 0x0001;
constexpr GLbitfield GL_MAP_WRITE_BIT = 0x0002;
constexpr GLbitfield GL_MAP_PERSISTENT_BIT = 0x0040;
constexpr GLbitfield GL_MAP_COHERENT_BIT = 0x0080;
constexpr GLbitfield GL_DYNAMIC_STORAGE_BIT = 0x0100;
constexpr GLbitfield GL_CLIENT_STORAGE_BIT = 0x0200;
constexpr GLbitfield GL_MAP_INVALIDATE_BUFFER_BIT = 0x0008;
constexpr GLbitfield GL_MAP_INVALIDATE_RANGE_BIT = 0x0004;
constexpr GLbitfield GL_MAP_FLUSH_EXPLICIT_BIT = 0x0010;
constexpr GLbitfield GL_MAP_UNSYNCHRONIZED_BIT = 0x0020;

// Texture targets
constexpr GLenum GL_TEXTURE_1D = 0x0DE0;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE_3D = 0x806F;
constexpr GLenum GL_TEXTURE_CUBE_MAP = 0x8513;
constexpr GLenum GL_TEXTURE_1D_ARRAY = 0x8C18;
constexpr GLenum GL_TEXTURE_2D_ARRAY = 0x8C1A;
constexpr GLenum GL_TEXTURE_CUBE_MAP_ARRAY = 0x9009;
constexpr GLenum GL_TEXTURE_2D_MULTISAMPLE = 0x9100;
constexpr GLenum GL_TEXTURE_2D_MULTISAMPLE_ARRAY = 0x9102;
constexpr GLenum GL_TEXTURE_BUFFER = 0x8C2A;

// Texture cube map faces
constexpr GLenum GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515;
constexpr GLenum GL_TEXTURE_CUBE_MAP_NEGATIVE_X = 0x8516;
constexpr GLenum GL_TEXTURE_CUBE_MAP_POSITIVE_Y = 0x8517;
constexpr GLenum GL_TEXTURE_CUBE_MAP_NEGATIVE_Y = 0x8518;
constexpr GLenum GL_TEXTURE_CUBE_MAP_POSITIVE_Z = 0x8519;
constexpr GLenum GL_TEXTURE_CUBE_MAP_NEGATIVE_Z = 0x851A;

// Texture parameters
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_TEXTURE_WRAP_R = 0x8072;
constexpr GLenum GL_TEXTURE_MIN_LOD = 0x813A;
constexpr GLenum GL_TEXTURE_MAX_LOD = 0x813B;
constexpr GLenum GL_TEXTURE_BASE_LEVEL = 0x813C;
constexpr GLenum GL_TEXTURE_MAX_LEVEL = 0x813D;
constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY = 0x84FE;
constexpr GLenum GL_TEXTURE_COMPARE_MODE = 0x884C;
constexpr GLenum GL_TEXTURE_COMPARE_FUNC = 0x884D;

// Texture units
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE1 = 0x84C1;
constexpr GLenum GL_TEXTURE2 = 0x84C2;
constexpr GLenum GL_TEXTURE3 = 0x84C3;
constexpr GLenum GL_TEXTURE4 = 0x84C4;
constexpr GLenum GL_TEXTURE5 = 0x84C5;
constexpr GLenum GL_TEXTURE6 = 0x84C6;
constexpr GLenum GL_TEXTURE7 = 0x84C7;
constexpr GLenum GL_TEXTURE8 = 0x84C8;
constexpr GLenum GL_TEXTURE9 = 0x84C9;
constexpr GLenum GL_TEXTURE10 = 0x84CA;
constexpr GLenum GL_TEXTURE11 = 0x84CB;
constexpr GLenum GL_TEXTURE12 = 0x84CC;
constexpr GLenum GL_TEXTURE13 = 0x84CD;
constexpr GLenum GL_TEXTURE14 = 0x84CE;
constexpr GLenum GL_TEXTURE15 = 0x84CF;
constexpr GLenum GL_MAX_TEXTURE_UNITS = 0x84E2;
constexpr GLenum GL_ACTIVE_TEXTURE = 0x84E0;

// Texture filter modes
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr GLenum GL_LINEAR_MIPMAP_NEAREST = 0x2701;
constexpr GLenum GL_NEAREST_MIPMAP_LINEAR = 0x2702;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;

// Texture wrap modes
constexpr GLenum GL_REPEAT = 0x2901;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_CLAMP_TO_BORDER = 0x812D;
constexpr GLenum GL_MIRRORED_REPEAT = 0x8370;
constexpr GLenum GL_MIRROR_CLAMP_TO_EDGE = 0x8743;

// Pixel formats
constexpr GLenum GL_RED = 0x1903;
constexpr GLenum GL_RG = 0x8227;
constexpr GLenum GL_RGB = 0x1907;
constexpr GLenum GL_BGR = 0x80E0;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_BGRA = 0x80E1;
constexpr GLenum GL_DEPTH_COMPONENT = 0x1902;
constexpr GLenum GL_DEPTH_STENCIL = 0x84F9;
constexpr GLenum GL_STENCIL_INDEX = 0x1901;

// Internal formats
constexpr GLenum GL_R8 = 0x8229;
constexpr GLenum GL_R16 = 0x822A;
constexpr GLenum GL_R16F = 0x822D;
constexpr GLenum GL_R32F = 0x822E;
constexpr GLenum GL_RG8 = 0x822B;
constexpr GLenum GL_RG16 = 0x822C;
constexpr GLenum GL_RG16F = 0x822F;
constexpr GLenum GL_RG32F = 0x8230;
constexpr GLenum GL_RGB8 = 0x8051;
constexpr GLenum GL_RGB16F = 0x881B;
constexpr GLenum GL_RGB32F = 0x8815;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_RGBA16 = 0x805B;
constexpr GLenum GL_RGBA16F = 0x881A;
constexpr GLenum GL_RGBA32F = 0x8814;
constexpr GLenum GL_SRGB8 = 0x8C41;
constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43;
constexpr GLenum GL_DEPTH_COMPONENT16 = 0x81A5;
constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_DEPTH_COMPONENT32F = 0x8CAC;
constexpr GLenum GL_DEPTH24_STENCIL8 = 0x88F0;
constexpr GLenum GL_DEPTH32F_STENCIL8 = 0x8CAD;

// Compressed formats (S3TC/DXT)
constexpr GLenum GL_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2;
constexpr GLenum GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3;
constexpr GLenum GL_COMPRESSED_SRGB_S3TC_DXT1_EXT = 0x8C4C;
constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT = 0x8C4D;
constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT = 0x8C4E;
constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT = 0x8C4F;

// Framebuffer
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_STENCIL_ATTACHMENT = 0x8D20;
constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT = 0x821A;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;

// Shader types
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_GEOMETRY_SHADER = 0x8DD9;
constexpr GLenum GL_TESS_CONTROL_SHADER = 0x8E88;
constexpr GLenum GL_TESS_EVALUATION_SHADER = 0x8E87;
constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;

// Shader status
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_VALIDATE_STATUS = 0x8B83;
constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum GL_SHADER_SOURCE_LENGTH = 0x8B88;

// Blend functions
constexpr GLenum GL_ZERO = 0;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_SRC_COLOR = 0x0300;
constexpr GLenum GL_ONE_MINUS_SRC_COLOR = 0x0301;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_DST_ALPHA = 0x0304;
constexpr GLenum GL_ONE_MINUS_DST_ALPHA = 0x0305;
constexpr GLenum GL_DST_COLOR = 0x0306;
constexpr GLenum GL_ONE_MINUS_DST_COLOR = 0x0307;
constexpr GLenum GL_SRC_ALPHA_SATURATE = 0x0308;

// Blend equations
constexpr GLenum GL_FUNC_ADD = 0x8006;
constexpr GLenum GL_FUNC_SUBTRACT = 0x800A;
constexpr GLenum GL_FUNC_REVERSE_SUBTRACT = 0x800B;
constexpr GLenum GL_MIN = 0x8007;
constexpr GLenum GL_MAX = 0x8008;

// Depth/stencil functions
constexpr GLenum GL_NEVER = 0x0200;
constexpr GLenum GL_LESS = 0x0201;
constexpr GLenum GL_EQUAL = 0x0202;
constexpr GLenum GL_LEQUAL = 0x0203;
constexpr GLenum GL_GREATER = 0x0204;
constexpr GLenum GL_NOTEQUAL = 0x0205;
constexpr GLenum GL_GEQUAL = 0x0206;
constexpr GLenum GL_ALWAYS = 0x0207;

// Stencil operations
constexpr GLenum GL_KEEP = 0x1E00;
constexpr GLenum GL_REPLACE = 0x1E01;
constexpr GLenum GL_INCR = 0x1E02;
constexpr GLenum GL_DECR = 0x1E03;
constexpr GLenum GL_INVERT = 0x150A;
constexpr GLenum GL_INCR_WRAP = 0x8507;
constexpr GLenum GL_DECR_WRAP = 0x8508;

// Face culling
constexpr GLenum GL_FRONT = 0x0404;
constexpr GLenum GL_BACK = 0x0405;
constexpr GLenum GL_FRONT_AND_BACK = 0x0408;
constexpr GLenum GL_CW = 0x0900;
constexpr GLenum GL_CCW = 0x0901;

// Polygon mode
constexpr GLenum GL_POINT = 0x1B00;
constexpr GLenum GL_LINE = 0x1B01;
constexpr GLenum GL_FILL = 0x1B02;

// Enable/disable caps
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_STENCIL_TEST = 0x0B90;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_POLYGON_OFFSET_FILL = 0x8037;
constexpr GLenum GL_MULTISAMPLE = 0x809D;
constexpr GLenum GL_FRAMEBUFFER_SRGB = 0x8DB9;
constexpr GLenum GL_PRIMITIVE_RESTART = 0x8F9D;
constexpr GLenum GL_PRIMITIVE_RESTART_FIXED_INDEX = 0x8D69;
constexpr GLenum GL_DEPTH_CLAMP = 0x864F;
constexpr GLenum GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F;
constexpr GLenum GL_PROGRAM_POINT_SIZE = 0x8642;

// Clear bits
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLbitfield GL_STENCIL_BUFFER_BIT = 0x00000400;

// Get parameters
constexpr GLenum GL_VENDOR = 0x1F00;
constexpr GLenum GL_RENDERER = 0x1F01;
constexpr GLenum GL_VERSION = 0x1F02;
constexpr GLenum GL_EXTENSIONS = 0x1F03;
constexpr GLenum GL_SHADING_LANGUAGE_VERSION = 0x8B8C;
constexpr GLenum GL_MAJOR_VERSION = 0x821B;
constexpr GLenum GL_MINOR_VERSION = 0x821C;
constexpr GLenum GL_NUM_EXTENSIONS = 0x821D;
constexpr GLenum GL_MAX_TEXTURE_SIZE = 0x0D33;
constexpr GLenum GL_MAX_3D_TEXTURE_SIZE = 0x8073;
constexpr GLenum GL_MAX_CUBE_MAP_TEXTURE_SIZE = 0x851C;
constexpr GLenum GL_MAX_ARRAY_TEXTURE_LAYERS = 0x88FF;
constexpr GLenum GL_MAX_TEXTURE_IMAGE_UNITS = 0x8872;
constexpr GLenum GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS = 0x8B4C;
constexpr GLenum GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS = 0x8B4D;
constexpr GLenum GL_MAX_VERTEX_ATTRIBS = 0x8869;
constexpr GLenum GL_MAX_VERTEX_UNIFORM_COMPONENTS = 0x8B4A;
constexpr GLenum GL_MAX_FRAGMENT_UNIFORM_COMPONENTS = 0x8B49;
constexpr GLenum GL_MAX_DRAW_BUFFERS = 0x8824;
constexpr GLenum GL_MAX_COLOR_ATTACHMENTS = 0x8CDF;
constexpr GLenum GL_MAX_RENDERBUFFER_SIZE = 0x84E8;
constexpr GLenum GL_MAX_VIEWPORT_DIMS = 0x0D3A;
constexpr GLenum GL_MAX_UNIFORM_BUFFER_BINDINGS = 0x8A2F;
constexpr GLenum GL_MAX_UNIFORM_BLOCK_SIZE = 0x8A30;

// Sync objects
constexpr GLenum GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117;
constexpr GLenum GL_ALREADY_SIGNALED = 0x911A;
constexpr GLenum GL_TIMEOUT_EXPIRED = 0x911B;
constexpr GLenum GL_CONDITION_SATISFIED = 0x911C;
constexpr GLenum GL_WAIT_FAILED = 0x911D;
constexpr GLbitfield GL_SYNC_FLUSH_COMMANDS_BIT = 0x00000001;
constexpr GLuint64 GL_TIMEOUT_IGNORED = 0xFFFFFFFFFFFFFFFFull;

// Debug output
constexpr GLenum GL_DEBUG_OUTPUT = 0x92E0;
constexpr GLenum GL_DEBUG_OUTPUT_SYNCHRONOUS = 0x8242;
constexpr GLenum GL_DEBUG_SOURCE_API = 0x8246;
constexpr GLenum GL_DEBUG_SOURCE_WINDOW_SYSTEM = 0x8247;
constexpr GLenum GL_DEBUG_SOURCE_SHADER_COMPILER = 0x8248;
constexpr GLenum GL_DEBUG_SOURCE_THIRD_PARTY = 0x8249;
constexpr GLenum GL_DEBUG_SOURCE_APPLICATION = 0x824A;
constexpr GLenum GL_DEBUG_SOURCE_OTHER = 0x824B;
constexpr GLenum GL_DEBUG_TYPE_ERROR = 0x824C;
constexpr GLenum GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR = 0x824D;
constexpr GLenum GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR = 0x824E;
constexpr GLenum GL_DEBUG_TYPE_PORTABILITY = 0x824F;
constexpr GLenum GL_DEBUG_TYPE_PERFORMANCE = 0x8250;
constexpr GLenum GL_DEBUG_TYPE_MARKER = 0x8268;
constexpr GLenum GL_DEBUG_TYPE_PUSH_GROUP = 0x8269;
constexpr GLenum GL_DEBUG_TYPE_POP_GROUP = 0x826A;
constexpr GLenum GL_DEBUG_TYPE_OTHER = 0x8251;
constexpr GLenum GL_DEBUG_SEVERITY_HIGH = 0x9146;
constexpr GLenum GL_DEBUG_SEVERITY_MEDIUM = 0x9147;
constexpr GLenum GL_DEBUG_SEVERITY_LOW = 0x9148;
constexpr GLenum GL_DEBUG_SEVERITY_NOTIFICATION = 0x826B;

// Error codes
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_INVALID_ENUM = 0x0500;
constexpr GLenum GL_INVALID_VALUE = 0x0501;
constexpr GLenum GL_INVALID_OPERATION = 0x0502;
constexpr GLenum GL_STACK_OVERFLOW = 0x0503;
constexpr GLenum GL_STACK_UNDERFLOW = 0x0504;
constexpr GLenum GL_OUT_OF_MEMORY = 0x0505;
constexpr GLenum GL_INVALID_FRAMEBUFFER_OPERATION = 0x0506;

// SPIR-V support (GL 4.6)
constexpr GLenum GL_SHADER_BINARY_FORMAT_SPIR_V = 0x9551;
constexpr GLenum GL_SPIR_V_BINARY = 0x9552;

// Memory barrier bits
constexpr GLbitfield GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT = 0x00000001;
constexpr GLbitfield GL_ELEMENT_ARRAY_BARRIER_BIT = 0x00000002;
constexpr GLbitfield GL_UNIFORM_BARRIER_BIT = 0x00000004;
constexpr GLbitfield GL_TEXTURE_FETCH_BARRIER_BIT = 0x00000008;
constexpr GLbitfield GL_SHADER_IMAGE_ACCESS_BARRIER_BIT = 0x00000020;
constexpr GLbitfield GL_COMMAND_BARRIER_BIT = 0x00000040;
constexpr GLbitfield GL_PIXEL_BUFFER_BARRIER_BIT = 0x00000080;
constexpr GLbitfield GL_TEXTURE_UPDATE_BARRIER_BIT = 0x00000100;
constexpr GLbitfield GL_BUFFER_UPDATE_BARRIER_BIT = 0x00000200;
constexpr GLbitfield GL_FRAMEBUFFER_BARRIER_BIT = 0x00000400;
constexpr GLbitfield GL_TRANSFORM_FEEDBACK_BARRIER_BIT = 0x00000800;
constexpr GLbitfield GL_ATOMIC_COUNTER_BARRIER_BIT = 0x00001000;
constexpr GLbitfield GL_SHADER_STORAGE_BARRIER_BIT = 0x00002000;
constexpr GLbitfield GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT = 0x00004000;
constexpr GLbitfield GL_QUERY_BUFFER_BARRIER_BIT = 0x00008000;
constexpr GLbitfield GL_ALL_BARRIER_BITS = 0xFFFFFFFF;

} // namespace MonsterEngine::OpenGL
