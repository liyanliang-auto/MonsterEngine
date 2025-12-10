// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Plugins/Interchange architecture

#pragma once

/**
 * @file MeshLoader.h
 * @brief Mesh loader interface and registry system
 * 
 * This file defines the mesh loading architecture:
 * - IMeshLoader: Interface for mesh file loaders
 * - FMeshLoaderRegistry: Singleton registry for loader plugins
 * - Support for OBJ, glTF, and extensible formats
 * 
 * The loader system follows a plugin pattern where different file formats
 * are handled by specialized loader implementations. The registry manages
 * loader discovery and selection based on file extension.
 * 
 * Usage:
 * @code
 * // Load a mesh file
 * FMeshBuilder Builder;
 * if (FMeshLoaderRegistry::Get().LoadMesh("model.obj", Builder))
 * {
 *     FStaticMesh* Mesh = Builder.Build(Device, "MyMesh");
 * }
 * @endcode
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointer.h"
#include "Core/Templates/UniquePtr.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Math/MathFwd.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"

// Forward declaration in MonsterRender namespace
namespace MonsterRender { namespace RHI { class IRHIDevice; } }

namespace MonsterEngine
{

// Forward declarations
class FMeshBuilder;
class FStaticMesh;

// Use MonsterRender::RHI types
using MonsterRender::RHI::IRHIDevice;

// ============================================================================
// Mesh Load Result
// ============================================================================

/**
 * @enum EMeshLoadResult
 * @brief Result codes for mesh loading operations
 */
enum class EMeshLoadResult : uint8
{
    Success = 0,            // Mesh loaded successfully
    FileNotFound,           // File does not exist
    UnsupportedFormat,      // No loader for this format
    ParseError,             // Error parsing file content
    InvalidData,            // File contains invalid mesh data
    OutOfMemory,            // Memory allocation failed
    Unknown                 // Unknown error
};

/**
 * Convert load result to string for logging
 * @param Result The result code
 * @return String description
 */
inline const char* MeshLoadResultToString(EMeshLoadResult Result)
{
    switch (Result)
    {
        case EMeshLoadResult::Success:           return "Success";
        case EMeshLoadResult::FileNotFound:      return "File not found";
        case EMeshLoadResult::UnsupportedFormat: return "Unsupported format";
        case EMeshLoadResult::ParseError:        return "Parse error";
        case EMeshLoadResult::InvalidData:       return "Invalid data";
        case EMeshLoadResult::OutOfMemory:       return "Out of memory";
        default:                                 return "Unknown error";
    }
}

// ============================================================================
// Mesh Load Options
// ============================================================================

/**
 * @struct FMeshLoadOptions
 * @brief Options for mesh loading operations
 */
struct FMeshLoadOptions
{
    /** Whether to compute normals if not present in file */
    bool bComputeNormals;
    
    /** Whether to compute tangents if not present in file */
    bool bComputeTangents;
    
    /** Whether to use smooth normals */
    bool bUseSmoothNormals;
    
    /** Whether to flip UV V coordinate (some formats use different conventions) */
    bool bFlipUVs;
    
    /** Whether to flip winding order (for coordinate system conversion) */
    bool bFlipWindingOrder;
    
    /** Scale factor to apply to positions */
    float Scale;
    
    /** Whether to merge vertices with same position */
    bool bMergeVertices;
    
    /** Threshold for vertex merging */
    float MergeThreshold;
    
    /** Whether to generate lightmap UVs */
    bool bGenerateLightmapUVs;
    
    /** Lightmap UV channel index */
    int32 LightmapUVChannel;
    
    /**
     * Default constructor
     */
    FORCEINLINE FMeshLoadOptions()
        : bComputeNormals(true)
        , bComputeTangents(true)
        , bUseSmoothNormals(true)
        , bFlipUVs(false)
        , bFlipWindingOrder(false)
        , Scale(1.0f)
        , bMergeVertices(false)
        , MergeThreshold(0.0001f)
        , bGenerateLightmapUVs(false)
        , LightmapUVChannel(1)
    {
    }
};

// ============================================================================
// Mesh Loader Interface
// ============================================================================

/**
 * @class IMeshLoader
 * @brief Interface for mesh file loaders
 * 
 * Implement this interface to add support for new mesh file formats.
 * Each loader handles one or more file extensions and converts the
 * file content into FMeshBuilder data.
 * 
 * Reference UE5: IInterchangeTranslator
 */
class IMeshLoader
{
public:
    /**
     * Virtual destructor
     */
    virtual ~IMeshLoader() = default;
    
    /**
     * Get the name of this loader
     * @return Loader name for identification
     */
    virtual const char* GetName() const = 0;
    
    /**
     * Get supported file extensions
     * @return Array of supported extensions (without dot, lowercase)
     */
    virtual TArray<String> GetSupportedExtensions() const = 0;
    
    /**
     * Check if this loader can handle a specific file
     * @param FilePath Path to the file
     * @return true if this loader can handle the file
     */
    virtual bool CanLoad(const String& FilePath) const;
    
    /**
     * Load a mesh file into a mesh builder
     * @param FilePath Path to the file
     * @param OutBuilder Builder to receive mesh data
     * @param Options Loading options
     * @return Load result code
     */
    virtual EMeshLoadResult Load(
        const String& FilePath,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options = FMeshLoadOptions()) = 0;
    
    /**
     * Load a mesh file from memory
     * @param Data Pointer to file data
     * @param DataSize Size of data in bytes
     * @param OutBuilder Builder to receive mesh data
     * @param Options Loading options
     * @return Load result code
     */
    virtual EMeshLoadResult LoadFromMemory(
        const void* Data,
        size_t DataSize,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options = FMeshLoadOptions());

    /**
     * Get file extension from path (lowercase, without dot)
     * @param FilePath File path
     * @return Extension string
     */
    static String GetExtension(const String& FilePath);

protected:
    
    /**
     * Read entire file into memory
     * @param FilePath File path
     * @param OutData Output data buffer
     * @return true if successful
     */
    static bool ReadFile(const String& FilePath, TArray<uint8>& OutData);
    
    /**
     * Read file as text
     * @param FilePath File path
     * @param OutText Output text string
     * @return true if successful
     */
    static bool ReadTextFile(const String& FilePath, String& OutText);
};

// ============================================================================
// Mesh Loader Registry
// ============================================================================

/**
 * @class FMeshLoaderRegistry
 * @brief Singleton registry for mesh loaders
 * 
 * Manages registration and lookup of mesh loaders by file extension.
 * Provides a central point for loading meshes from various formats.
 * 
 * Reference UE5: IInterchangeManager
 */
class FMeshLoaderRegistry
{
public:
    /**
     * Get the singleton instance
     * @return Reference to the registry
     */
    static FMeshLoaderRegistry& Get();
    
    /**
     * Register a mesh loader
     * @param Loader Loader to register (registry takes ownership)
     */
    void RegisterLoader(TUniquePtr<IMeshLoader> Loader);
    
    /**
     * Register a mesh loader (shared ownership)
     * @param Loader Loader to register
     */
    void RegisterLoader(TSharedPtr<IMeshLoader> Loader);
    
    /**
     * Unregister a loader by name
     * @param LoaderName Name of the loader to remove
     * @return true if loader was found and removed
     */
    bool UnregisterLoader(const String& LoaderName);
    
    /**
     * Find a loader for a specific file
     * @param FilePath Path to the file
     * @return Pointer to loader, or nullptr if none found
     */
    IMeshLoader* FindLoader(const String& FilePath) const;
    
    /**
     * Find a loader by extension
     * @param Extension File extension (without dot, case-insensitive)
     * @return Pointer to loader, or nullptr if none found
     */
    IMeshLoader* FindLoaderByExtension(const String& Extension) const;
    
    /**
     * Check if a file format is supported
     * @param FilePath Path to the file
     * @return true if a loader exists for this format
     */
    bool IsFormatSupported(const String& FilePath) const;
    
    /**
     * Get all supported extensions
     * @return Array of supported extensions
     */
    TArray<String> GetSupportedExtensions() const;
    
    /**
     * Load a mesh file
     * @param FilePath Path to the file
     * @param OutBuilder Builder to receive mesh data
     * @param Options Loading options
     * @return Load result code
     */
    EMeshLoadResult LoadMesh(
        const String& FilePath,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options = FMeshLoadOptions()) const;
    
    /**
     * Load a mesh file and build directly to FStaticMesh
     * @param FilePath Path to the file
     * @param Device RHI device for buffer creation
     * @param Options Loading options
     * @return Created mesh, or nullptr on failure
     */
    FStaticMesh* LoadStaticMesh(
        const String& FilePath,
        IRHIDevice* Device,
        const FMeshLoadOptions& Options = FMeshLoadOptions()) const;
    
    /**
     * Get the number of registered loaders
     * @return Loader count
     */
    int32 GetNumLoaders() const { return Loaders.Num(); }
    
    /**
     * Initialize default loaders (OBJ, glTF)
     * Called automatically on first access
     */
    void InitializeDefaultLoaders();

private:
    /**
     * Private constructor (singleton)
     */
    FMeshLoaderRegistry();
    
    /**
     * Destructor
     */
    ~FMeshLoaderRegistry();
    
    // Prevent copying
    FMeshLoaderRegistry(const FMeshLoaderRegistry&) = delete;
    FMeshLoaderRegistry& operator=(const FMeshLoaderRegistry&) = delete;
    
    /** Registered loaders */
    TArray<TSharedPtr<IMeshLoader>> Loaders;
    
    /** Extension to loader mapping for fast lookup */
    TMap<String, IMeshLoader*> ExtensionMap;
    
    /** Whether default loaders have been initialized */
    bool bDefaultLoadersInitialized;
    
    /**
     * Rebuild extension map after loader changes
     */
    void RebuildExtensionMap();
    
    /**
     * Convert extension to lowercase
     * @param Extension Input extension
     * @return Lowercase extension
     */
    static String NormalizeExtension(const String& Extension);
};

// ============================================================================
// OBJ Mesh Loader
// ============================================================================

/**
 * @class FOBJMeshLoader
 * @brief Loader for Wavefront OBJ mesh files
 * 
 * Supports:
 * - Vertex positions (v)
 * - Texture coordinates (vt)
 * - Vertex normals (vn)
 * - Faces (f) with various formats
 * - Material groups (usemtl)
 * - Object groups (o, g)
 * 
 * Reference: https://en.wikipedia.org/wiki/Wavefront_.obj_file
 */
class FOBJMeshLoader : public IMeshLoader
{
public:
    /**
     * Constructor
     */
    FOBJMeshLoader() = default;
    
    /**
     * Destructor
     */
    virtual ~FOBJMeshLoader() = default;
    
    // IMeshLoader interface
    virtual const char* GetName() const override { return "OBJ Loader"; }
    virtual TArray<String> GetSupportedExtensions() const override;
    virtual EMeshLoadResult Load(
        const String& FilePath,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options) override;
    virtual EMeshLoadResult LoadFromMemory(
        const void* Data,
        size_t DataSize,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options) override;

private:
    /**
     * Parse OBJ file content
     * @param Content File content as string
     * @param OutBuilder Builder to receive data
     * @param Options Loading options
     * @return Load result
     */
    EMeshLoadResult ParseOBJ(
        const String& Content,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options);
    
    /**
     * Parse a face line (f v/vt/vn v/vt/vn v/vt/vn ...)
     * @param Line The face line
     * @param Positions Position array
     * @param TexCoords Texture coordinate array
     * @param Normals Normal array
     * @param OutBuilder Builder to add face to
     * @param CurrentMaterial Current material index
     * @param Options Loading options
     */
    void ParseFace(
        const String& Line,
        const TArray<Math::FVector3f>& Positions,
        const TArray<Math::FVector2f>& TexCoords,
        const TArray<Math::FVector3f>& Normals,
        FMeshBuilder& OutBuilder,
        int32 CurrentMaterial,
        const FMeshLoadOptions& Options);
};

// ============================================================================
// glTF Mesh Loader
// ============================================================================

/**
 * @class FGLTFMeshLoader
 * @brief Loader for glTF 2.0 mesh files
 * 
 * Supports:
 * - glTF JSON format (.gltf)
 * - glTF Binary format (.glb)
 * - Embedded and external buffers
 * - Multiple meshes and primitives
 * - PBR materials (basic support)
 * 
 * Reference: https://www.khronos.org/gltf/
 */
class FGLTFMeshLoader : public IMeshLoader
{
public:
    /**
     * Constructor
     */
    FGLTFMeshLoader() = default;
    
    /**
     * Destructor
     */
    virtual ~FGLTFMeshLoader() = default;
    
    // IMeshLoader interface
    virtual const char* GetName() const override { return "glTF Loader"; }
    virtual TArray<String> GetSupportedExtensions() const override;
    virtual EMeshLoadResult Load(
        const String& FilePath,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options) override;
    virtual EMeshLoadResult LoadFromMemory(
        const void* Data,
        size_t DataSize,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options) override;

private:
    /**
     * Parse glTF JSON content
     * @param JsonContent JSON string
     * @param BasePath Base path for external resources
     * @param OutBuilder Builder to receive data
     * @param Options Loading options
     * @return Load result
     */
    EMeshLoadResult ParseGLTF(
        const String& JsonContent,
        const String& BasePath,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options);
    
    /**
     * Parse GLB binary format
     * @param Data Binary data
     * @param DataSize Data size
     * @param OutBuilder Builder to receive data
     * @param Options Loading options
     * @return Load result
     */
    EMeshLoadResult ParseGLB(
        const void* Data,
        size_t DataSize,
        FMeshBuilder& OutBuilder,
        const FMeshLoadOptions& Options);
};

} // namespace MonsterEngine
