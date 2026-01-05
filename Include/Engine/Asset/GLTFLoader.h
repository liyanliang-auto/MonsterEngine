// Copyright Monster Engine. All Rights Reserved.
// Reference: Filament gltfio/AssetLoader, UE5 GLTFImporter

#pragma once

/**
 * @file GLTFLoader.h
 * @brief glTF 2.0 asset loader
 * 
 * Provides functionality to load glTF/GLB files and convert them
 * to engine-specific data structures. Supports both Vulkan and OpenGL
 * backends through the RHI abstraction layer.
 * 
 * Reference:
 * - Filament: libs/gltfio/src/AssetLoader.cpp
 * - UE5: Engine/Plugins/Editor/GLTFImporter/Source/GLTFImporter
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/String.h"

namespace MonsterEngine
{

// Forward declarations
struct FGLTFModel;
struct FGLTFMesh;
struct FGLTFPrimitive;
struct FGLTFMaterial;
struct FGLTFImage;

// ============================================================================
// glTF Loader Options
// ============================================================================

/**
 * @struct FGLTFLoadOptions
 * @brief Options for glTF loading
 */
struct FGLTFLoadOptions
{
    /** Whether to load textures */
    bool bLoadTextures = true;
    
    /** Whether to generate tangents if not present */
    bool bGenerateTangents = true;
    
    /** Whether to generate normals if not present */
    bool bGenerateNormals = true;
    
    /** Whether to flip UV Y coordinate (for OpenGL compatibility) */
    bool bFlipUVs = false;
    
    /** Whether to normalize vertex positions to unit scale */
    bool bNormalizeScale = false;
    
    /** Whether to compute bounding boxes */
    bool bComputeBounds = true;
    
    /** Whether to merge primitives with same material */
    bool bMergePrimitives = false;
    
    /** Maximum texture resolution (0 = no limit) */
    uint32 MaxTextureResolution = 0;
    
    /** Default constructor */
    FGLTFLoadOptions() = default;
};

// ============================================================================
// glTF Loader Result
// ============================================================================

/**
 * @enum EGLTFLoadResult
 * @brief Result codes for glTF loading operations
 */
enum class EGLTFLoadResult : uint8
{
    Success = 0,
    FileNotFound,
    InvalidFormat,
    ParseError,
    UnsupportedVersion,
    MissingBuffers,
    TextureLoadFailed,
    OutOfMemory,
    Unknown
};

/**
 * @brief Convert load result to string
 */
const char* GLTFLoadResultToString(EGLTFLoadResult Result);

// ============================================================================
// glTF Loader
// ============================================================================

/**
 * @class FGLTFLoader
 * @brief Loads glTF 2.0 assets from files
 * 
 * This class handles parsing glTF/GLB files and extracting mesh,
 * material, and texture data. It supports:
 * - glTF JSON format (.gltf)
 * - glTF Binary format (.glb)
 * - Embedded and external buffers
 * - Embedded and external images
 * - PBR metallic-roughness materials
 * 
 * Usage:
 * @code
 * FGLTFLoader loader;
 * TSharedPtr<FGLTFModel> model = loader.LoadFromFile("model.gltf");
 * if (model) {
 *     // Use model data
 * }
 * @endcode
 */
class FGLTFLoader
{
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================
    
    /**
     * Default constructor
     */
    FGLTFLoader();
    
    /**
     * Destructor
     */
    ~FGLTFLoader();
    
    // Non-copyable
    FGLTFLoader(const FGLTFLoader&) = delete;
    FGLTFLoader& operator=(const FGLTFLoader&) = delete;
    
    // ========================================================================
    // Loading Methods
    // ========================================================================
    
    /**
     * Load a glTF model from file
     * @param FilePath Path to .gltf or .glb file
     * @param Options Loading options
     * @return Loaded model or nullptr on failure
     */
    TSharedPtr<FGLTFModel> LoadFromFile(
        const String& FilePath,
        const FGLTFLoadOptions& Options = FGLTFLoadOptions());
    
    /**
     * Load a glTF model from memory
     * @param Data Raw file data
     * @param DataSize Size of data in bytes
     * @param BasePath Base path for resolving external resources
     * @param Options Loading options
     * @return Loaded model or nullptr on failure
     */
    TSharedPtr<FGLTFModel> LoadFromMemory(
        const void* Data,
        uint64 DataSize,
        const String& BasePath,
        const FGLTFLoadOptions& Options = FGLTFLoadOptions());
    
    /**
     * Get the last error result
     * @return Last error code
     */
    EGLTFLoadResult GetLastError() const { return m_lastError; }
    
    /**
     * Get the last error message
     * @return Last error message
     */
    const String& GetLastErrorMessage() const { return m_lastErrorMessage; }
    
    // ========================================================================
    // Static Utility Methods
    // ========================================================================
    
    /**
     * Check if a file is a valid glTF file
     * @param FilePath Path to file
     * @return true if file appears to be valid glTF
     */
    static bool IsValidGLTFFile(const String& FilePath);
    
    /**
     * Get file extension for glTF files
     * @param FilePath Path to file
     * @return true if .gltf or .glb extension
     */
    static bool HasGLTFExtension(const String& FilePath);

private:
    // ========================================================================
    // Internal Parsing Methods
    // ========================================================================
    
    /**
     * Parse cgltf data into FGLTFModel
     * @param Data cgltf parsed data
     * @param BasePath Base path for external resources
     * @param Options Loading options
     * @param OutModel Output model
     * @return true on success
     */
    bool _parseGLTFData(
        struct cgltf_data* Data,
        const String& BasePath,
        const FGLTFLoadOptions& Options,
        FGLTFModel& OutModel);
    
    /**
     * Parse meshes from cgltf data
     */
    bool _parseMeshes(
        struct cgltf_data* Data,
        const FGLTFLoadOptions& Options,
        FGLTFModel& OutModel);
    
    /**
     * Parse a single primitive
     */
    bool _parsePrimitive(
        struct cgltf_primitive* Primitive,
        const FGLTFLoadOptions& Options,
        FGLTFPrimitive& OutPrimitive);
    
    /**
     * Parse materials from cgltf data
     */
    bool _parseMaterials(
        struct cgltf_data* Data,
        FGLTFModel& OutModel);
    
    /**
     * Parse textures and images from cgltf data
     */
    bool _parseTextures(
        struct cgltf_data* Data,
        const String& BasePath,
        const FGLTFLoadOptions& Options,
        FGLTFModel& OutModel);
    
    /**
     * Parse nodes and scenes from cgltf data
     */
    bool _parseNodes(
        struct cgltf_data* Data,
        FGLTFModel& OutModel);
    
    /**
     * Load image data from file or embedded buffer
     */
    bool _loadImageData(
        struct cgltf_image* Image,
        const String& BasePath,
        const FGLTFLoadOptions& Options,
        FGLTFImage& OutImage);
    
    /**
     * Generate tangents for a primitive
     */
    void _generateTangents(FGLTFPrimitive& Primitive);
    
    /**
     * Generate normals for a primitive
     */
    void _generateNormals(FGLTFPrimitive& Primitive);
    
    /**
     * Compute bounding box for model
     */
    void _computeBounds(FGLTFModel& Model);
    
    /**
     * Set error state
     */
    void _setError(EGLTFLoadResult Error, const String& Message);

private:
    /** Last error code */
    EGLTFLoadResult m_lastError;
    
    /** Last error message */
    String m_lastErrorMessage;
};

} // namespace MonsterEngine

// Note: FGLTFToStaticMeshConverter is defined in GLTFToStaticMeshConverter.h
// to avoid circular dependencies with StaticMesh.h
