// Copyright Monster Engine. All Rights Reserved.
// Reference: Filament gltfio, UE5 glTF importer

#pragma once

/**
 * @file GLTFTypes.h
 * @brief glTF 2.0 data type definitions
 * 
 * This file defines data structures for representing glTF 2.0 assets.
 * These structures are used as intermediate representation between
 * the raw glTF data and engine-specific resources.
 * 
 * Reference:
 * - glTF 2.0 Specification: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
 * - Filament: libs/gltfio/include/gltfio/FilamentAsset.h
 * - UE5: Engine/Plugins/Editor/GLTFImporter
 */

#include "Core/CoreTypes.h"
#include "Core/Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Containers/String.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Vector4.h"
#include "Math/Matrix.h"
#include "Math/Quat.h"
#include "Math/Box.h"

namespace MonsterEngine
{

// Note: Math types are already defined via includes above

// ============================================================================
// glTF Enumerations
// ============================================================================

/**
 * @enum EGLTFAlphaMode
 * @brief Alpha blending mode for materials
 */
enum class EGLTFAlphaMode : uint8
{
    Opaque = 0,     // Fully opaque
    Mask,           // Alpha cutoff
    Blend           // Alpha blending
};

/**
 * @enum EGLTFPrimitiveMode
 * @brief Primitive topology type
 */
enum class EGLTFPrimitiveMode : uint8
{
    Points = 0,
    Lines = 1,
    LineLoop = 2,
    LineStrip = 3,
    Triangles = 4,
    TriangleStrip = 5,
    TriangleFan = 6
};

/**
 * @enum EGLTFComponentType
 * @brief Component data type for accessors
 */
enum class EGLTFComponentType : uint16
{
    Byte = 5120,
    UnsignedByte = 5121,
    Short = 5122,
    UnsignedShort = 5123,
    UnsignedInt = 5125,
    Float = 5126
};

/**
 * @enum EGLTFAccessorType
 * @brief Accessor element type
 */
enum class EGLTFAccessorType : uint8
{
    Scalar = 0,
    Vec2,
    Vec3,
    Vec4,
    Mat2,
    Mat3,
    Mat4
};

/**
 * @enum EGLTFTextureFilter
 * @brief Texture filtering mode
 */
enum class EGLTFTextureFilter : uint16
{
    Nearest = 9728,
    Linear = 9729,
    NearestMipmapNearest = 9984,
    LinearMipmapNearest = 9985,
    NearestMipmapLinear = 9986,
    LinearMipmapLinear = 9987
};

/**
 * @enum EGLTFTextureWrap
 * @brief Texture wrapping mode
 */
enum class EGLTFTextureWrap : uint16
{
    ClampToEdge = 33071,
    MirroredRepeat = 33648,
    Repeat = 10497
};

// ============================================================================
// glTF Texture Info
// ============================================================================

/**
 * @struct FGLTFTextureInfo
 * @brief Reference to a texture with UV set and transform
 */
struct FGLTFTextureInfo
{
    /** Index of the texture in the textures array (-1 if not set) */
    int32 TextureIndex = -1;
    
    /** UV coordinate set index */
    int32 TexCoordIndex = 0;
    
    /** Texture transform scale */
    Math::FVector2f Scale = Math::FVector2f(1.0f, 1.0f);
    
    /** Texture transform offset */
    Math::FVector2f Offset = Math::FVector2f(0.0f, 0.0f);
    
    /** Texture transform rotation (radians) */
    float Rotation = 0.0f;
    
    /** Check if texture is valid */
    bool IsValid() const { return TextureIndex >= 0; }
};

/**
 * @struct FGLTFNormalTextureInfo
 * @brief Normal map texture info with scale
 */
struct FGLTFNormalTextureInfo : public FGLTFTextureInfo
{
    /** Normal map scale factor */
    float Scale = 1.0f;
};

/**
 * @struct FGLTFOcclusionTextureInfo
 * @brief Occlusion texture info with strength
 */
struct FGLTFOcclusionTextureInfo : public FGLTFTextureInfo
{
    /** Occlusion strength */
    float Strength = 1.0f;
};

// ============================================================================
// glTF Material
// ============================================================================

/**
 * @struct FGLTFMaterial
 * @brief PBR material definition from glTF
 * 
 * Represents a glTF PBR metallic-roughness material with all
 * standard properties and textures.
 */
struct FGLTFMaterial
{
    /** Material name */
    String Name;
    
    // ========================================================================
    // PBR Metallic-Roughness
    // ========================================================================
    
    /** Base color factor (RGBA, linear) */
    Math::FVector4f BaseColorFactor = Math::FVector4f(1.0f, 1.0f, 1.0f, 1.0f);
    
    /** Metallic factor [0, 1] */
    float MetallicFactor = 1.0f;
    
    /** Roughness factor [0, 1] */
    float RoughnessFactor = 1.0f;
    
    /** Base color texture */
    FGLTFTextureInfo BaseColorTexture;
    
    /** Metallic-roughness texture (B=metallic, G=roughness) */
    FGLTFTextureInfo MetallicRoughnessTexture;
    
    // ========================================================================
    // Common Properties
    // ========================================================================
    
    /** Normal map texture */
    FGLTFNormalTextureInfo NormalTexture;
    
    /** Occlusion texture (R channel) */
    FGLTFOcclusionTextureInfo OcclusionTexture;
    
    /** Emissive texture */
    FGLTFTextureInfo EmissiveTexture;
    
    /** Emissive factor (RGB, linear) */
    Math::FVector3f EmissiveFactor = Math::FVector3f(0.0f, 0.0f, 0.0f);
    
    // ========================================================================
    // Alpha Mode
    // ========================================================================
    
    /** Alpha rendering mode */
    EGLTFAlphaMode AlphaMode = EGLTFAlphaMode::Opaque;
    
    /** Alpha cutoff threshold (for Mask mode) */
    float AlphaCutoff = 0.5f;
    
    /** Whether material is double-sided */
    bool bDoubleSided = false;
    
    // ========================================================================
    // Extensions (KHR_materials_*)
    // ========================================================================
    
    /** Whether unlit extension is used */
    bool bUnlit = false;
    
    /** Clear coat intensity (KHR_materials_clearcoat) */
    float ClearCoatFactor = 0.0f;
    
    /** Clear coat roughness */
    float ClearCoatRoughnessFactor = 0.0f;
    
    /** IOR (KHR_materials_ior) */
    float IOR = 1.5f;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    FGLTFMaterial() = default;
    
    /**
     * Check if material has any textures
     */
    bool HasTextures() const
    {
        return BaseColorTexture.IsValid() ||
               MetallicRoughnessTexture.IsValid() ||
               NormalTexture.IsValid() ||
               OcclusionTexture.IsValid() ||
               EmissiveTexture.IsValid();
    }
};

// ============================================================================
// glTF Texture
// ============================================================================

/**
 * @struct FGLTFSampler
 * @brief Texture sampler settings
 */
struct FGLTFSampler
{
    /** Magnification filter */
    EGLTFTextureFilter MagFilter = EGLTFTextureFilter::Linear;
    
    /** Minification filter */
    EGLTFTextureFilter MinFilter = EGLTFTextureFilter::LinearMipmapLinear;
    
    /** U wrap mode */
    EGLTFTextureWrap WrapS = EGLTFTextureWrap::Repeat;
    
    /** V wrap mode */
    EGLTFTextureWrap WrapT = EGLTFTextureWrap::Repeat;
    
    /** Sampler name */
    String Name;
};

/**
 * @struct FGLTFImage
 * @brief Image data from glTF
 */
struct FGLTFImage
{
    /** Image name */
    String Name;
    
    /** URI or embedded data reference */
    String URI;
    
    /** MIME type (image/png, image/jpeg, etc.) */
    String MimeType;
    
    /** Buffer view index for embedded images (-1 if external) */
    int32 BufferViewIndex = -1;
    
    /** Decoded image data (RGBA) */
    TArray<uint8> Data;
    
    /** Image width */
    uint32 Width = 0;
    
    /** Image height */
    uint32 Height = 0;
    
    /** Number of channels */
    uint32 Channels = 4;
    
    /** Whether image data is loaded */
    bool bIsLoaded = false;
};

/**
 * @struct FGLTFTexture
 * @brief Texture definition combining image and sampler
 */
struct FGLTFTexture
{
    /** Texture name */
    String Name;
    
    /** Source image index */
    int32 ImageIndex = -1;
    
    /** Sampler index (-1 for default sampler) */
    int32 SamplerIndex = -1;
};

// ============================================================================
// glTF Primitive
// ============================================================================

/**
 * @struct FGLTFPrimitive
 * @brief A single drawable primitive within a mesh
 * 
 * Contains vertex attribute data and material reference.
 */
struct FGLTFPrimitive
{
    /** Primitive mode (triangles, lines, etc.) */
    EGLTFPrimitiveMode Mode = EGLTFPrimitiveMode::Triangles;
    
    /** Material index (-1 for default material) */
    int32 MaterialIndex = -1;
    
    // ========================================================================
    // Vertex Attributes (unpacked)
    // ========================================================================
    
    /** Vertex positions */
    TArray<Math::FVector3f> Positions;
    
    /** Vertex normals */
    TArray<Math::FVector3f> Normals;
    
    /** Vertex tangents (XYZ=tangent, W=handedness) */
    TArray<Math::FVector4f> Tangents;
    
    /** Primary UV coordinates */
    TArray<Math::FVector2f> TexCoords0;
    
    /** Secondary UV coordinates */
    TArray<Math::FVector2f> TexCoords1;
    
    /** Vertex colors */
    TArray<Math::FVector4f> Colors;
    
    /** Triangle indices */
    TArray<uint32> Indices;
    
    // ========================================================================
    // Bounds
    // ========================================================================
    
    /** Bounding box minimum */
    Math::FVector3f BoundsMin = Math::FVector3f(0.0f, 0.0f, 0.0f);
    
    /** Bounding box maximum */
    Math::FVector3f BoundsMax = Math::FVector3f(0.0f, 0.0f, 0.0f);
    
    // ========================================================================
    // Methods
    // ========================================================================
    
    /**
     * Get vertex count
     */
    uint32 GetVertexCount() const { return static_cast<uint32>(Positions.Num()); }
    
    /**
     * Get index count
     */
    uint32 GetIndexCount() const { return static_cast<uint32>(Indices.Num()); }
    
    /**
     * Get triangle count
     */
    uint32 GetTriangleCount() const { return GetIndexCount() / 3; }
    
    /**
     * Check if primitive has normals
     */
    bool HasNormals() const { return Normals.Num() > 0; }
    
    /**
     * Check if primitive has tangents
     */
    bool HasTangents() const { return Tangents.Num() > 0; }
    
    /**
     * Check if primitive has texture coordinates
     */
    bool HasTexCoords() const { return TexCoords0.Num() > 0; }
    
    /**
     * Check if primitive has vertex colors
     */
    bool HasColors() const { return Colors.Num() > 0; }
    
    /**
     * Check if primitive is valid
     */
    bool IsValid() const { return Positions.Num() > 0; }
};

// ============================================================================
// glTF Mesh
// ============================================================================

/**
 * @struct FGLTFMesh
 * @brief A mesh containing one or more primitives
 */
struct FGLTFMesh
{
    /** Mesh name */
    String Name;
    
    /** Primitives in this mesh */
    TArray<FGLTFPrimitive> Primitives;
    
    /**
     * Get total vertex count across all primitives
     */
    uint32 GetTotalVertexCount() const
    {
        uint32 Total = 0;
        for (const FGLTFPrimitive& Prim : Primitives)
        {
            Total += Prim.GetVertexCount();
        }
        return Total;
    }
    
    /**
     * Get total triangle count across all primitives
     */
    uint32 GetTotalTriangleCount() const
    {
        uint32 Total = 0;
        for (const FGLTFPrimitive& Prim : Primitives)
        {
            Total += Prim.GetTriangleCount();
        }
        return Total;
    }
};

// ============================================================================
// glTF Node
// ============================================================================

/**
 * @struct FGLTFNode
 * @brief A node in the scene hierarchy
 */
struct FGLTFNode
{
    /** Node name */
    String Name;
    
    /** Mesh index (-1 if no mesh) */
    int32 MeshIndex = -1;
    
    /** Child node indices */
    TArray<int32> Children;
    
    /** Parent node index (-1 if root) */
    int32 ParentIndex = -1;
    
    // ========================================================================
    // Transform (TRS or Matrix)
    // ========================================================================
    
    /** Translation */
    Math::FVector3f Translation = Math::FVector3f(0.0f, 0.0f, 0.0f);
    
    /** Rotation (quaternion) */
    Math::FQuat4f Rotation = Math::FQuat4f(0.0f, 0.0f, 0.0f, 1.0f);
    
    /** Scale */
    Math::FVector3f Scale = Math::FVector3f(1.0f, 1.0f, 1.0f);
    
    /** Whether transform is specified as matrix */
    bool bHasMatrix = false;
    
    /** Local transform matrix (if bHasMatrix is true) */
    Math::FMatrix LocalMatrix;
    
    // ========================================================================
    // Methods
    // ========================================================================
    
    /**
     * Get local transform matrix
     */
    Math::FMatrix GetLocalTransform() const;
    
    /**
     * Check if node has a mesh
     */
    bool HasMesh() const { return MeshIndex >= 0; }
    
    /**
     * Check if node has children
     */
    bool HasChildren() const { return Children.Num() > 0; }
};

// ============================================================================
// glTF Scene
// ============================================================================

/**
 * @struct FGLTFScene
 * @brief A scene containing root nodes
 */
struct FGLTFScene
{
    /** Scene name */
    String Name;
    
    /** Root node indices */
    TArray<int32> RootNodes;
};

// ============================================================================
// glTF Model (Complete Asset)
// ============================================================================

/**
 * @struct FGLTFModel
 * @brief Complete glTF model containing all data
 * 
 * This is the main container for all glTF data after parsing.
 * It owns all meshes, materials, textures, and scene hierarchy.
 */
struct FGLTFModel
{
    /** Asset name (from file or generator) */
    String Name;
    
    /** Source file path */
    String SourcePath;
    
    /** glTF version */
    String Version;
    
    /** Generator name */
    String Generator;
    
    // ========================================================================
    // Data Arrays
    // ========================================================================
    
    /** All meshes in the model */
    TArray<FGLTFMesh> Meshes;
    
    /** All materials in the model */
    TArray<FGLTFMaterial> Materials;
    
    /** All textures in the model */
    TArray<FGLTFTexture> Textures;
    
    /** All images in the model */
    TArray<FGLTFImage> Images;
    
    /** All samplers in the model */
    TArray<FGLTFSampler> Samplers;
    
    /** All nodes in the model */
    TArray<FGLTFNode> Nodes;
    
    /** All scenes in the model */
    TArray<FGLTFScene> Scenes;
    
    /** Default scene index (-1 if not specified) */
    int32 DefaultSceneIndex = -1;
    
    // ========================================================================
    // Bounds
    // ========================================================================
    
    /** Overall bounding box */
    Math::FBox3f Bounds;
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    /**
     * Get total vertex count
     */
    uint32 GetTotalVertexCount() const
    {
        uint32 Total = 0;
        for (const FGLTFMesh& Mesh : Meshes)
        {
            Total += Mesh.GetTotalVertexCount();
        }
        return Total;
    }
    
    /**
     * Get total triangle count
     */
    uint32 GetTotalTriangleCount() const
    {
        uint32 Total = 0;
        for (const FGLTFMesh& Mesh : Meshes)
        {
            Total += Mesh.GetTotalTriangleCount();
        }
        return Total;
    }
    
    /**
     * Get mesh count
     */
    int32 GetMeshCount() const { return Meshes.Num(); }
    
    /**
     * Get material count
     */
    int32 GetMaterialCount() const { return Materials.Num(); }
    
    /**
     * Get texture count
     */
    int32 GetTextureCount() const { return Textures.Num(); }
    
    /**
     * Get node count
     */
    int32 GetNodeCount() const { return Nodes.Num(); }
    
    /**
     * Check if model is valid
     */
    bool IsValid() const { return Meshes.Num() > 0; }
    
    /**
     * Clear all data
     */
    void Clear()
    {
        Meshes.Empty();
        Materials.Empty();
        Textures.Empty();
        Images.Empty();
        Samplers.Empty();
        Nodes.Empty();
        Scenes.Empty();
        DefaultSceneIndex = -1;
    }
};

} // namespace MonsterEngine
