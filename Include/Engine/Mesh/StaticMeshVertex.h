// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Source/Runtime/Engine/Public/Rendering/StaticMeshVertexBuffer.h

#pragma once

/**
 * @file StaticMeshVertex.h
 * @brief Static mesh vertex structures and types
 * 
 * This file defines vertex structures for static meshes, supporting both
 * standard and high-precision formats. Following UE5's approach, we separate
 * position data from other vertex attributes for efficient GPU access patterns.
 * 
 * Vertex data is organized into separate buffers:
 * - Position buffer: FVector3f positions only
 * - Tangent buffer: Normal and tangent vectors (packed)
 * - TexCoord buffer: UV coordinates (can be half or full precision)
 * - Color buffer: Vertex colors (optional)
 */

#include "Core/CoreTypes.h"
#include "Math/MathFwd.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Vector4.h"
#include "Engine/Mesh/PackedNormal.h"
#include "Engine/Mesh/VertexFactory.h"

namespace MonsterEngine
{

// ============================================================================
// Maximum Texture Coordinates
// ============================================================================

/** Maximum number of texture coordinate sets supported */
constexpr uint32 MAX_STATIC_MESH_TEXCOORDS = 8;

/** Maximum number of LOD levels */
constexpr uint32 MAX_STATIC_MESH_LODS = 8;

// ============================================================================
// Tangent Basis Precision
// ============================================================================

/**
 * @enum EStaticMeshVertexTangentBasisType
 * @brief Precision level for tangent basis storage
 * 
 * Controls whether tangent/normal vectors use 8-bit or 16-bit precision.
 * Higher precision is useful for detailed normal maps and smooth surfaces.
 */
enum class EStaticMeshVertexTangentBasisType : uint8
{
    /** Default precision - 8-bit per component (FPackedNormal) */
    Default = 0,
    
    /** High precision - 16-bit per component (FPackedRGBA16N) */
    HighPrecision = 1
};

/**
 * @enum EStaticMeshVertexUVType
 * @brief Precision level for UV coordinate storage
 * 
 * Controls whether UV coordinates use half-float or full-float precision.
 * Higher precision is useful for large UV ranges or tiled textures.
 */
enum class EStaticMeshVertexUVType : uint8
{
    /** Default precision - 16-bit half-float (FVector2DHalf) */
    Default = 0,
    
    /** High precision - 32-bit float (FVector2f) */
    HighPrecision = 1
};

// ============================================================================
// Tangent Data Structures
// ============================================================================

/**
 * @struct FStaticMeshVertexTangent
 * @brief Standard precision tangent data (8-bit per component)
 * 
 * Stores tangent (X) and normal (Z) vectors in packed format.
 * The binormal (Y) is computed from X and Z using the sign stored in Z.W.
 * 
 * Memory layout: [TangentX:4][TangentZ:4] = 8 bytes
 */
struct FStaticMeshVertexTangent
{
    /** Tangent vector (X axis of tangent space) */
    FPackedNormal TangentX;
    
    /** Normal vector (Z axis of tangent space), W stores binormal sign */
    FPackedNormal TangentZ;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor - initializes to default tangent space
     */
    FORCEINLINE FStaticMeshVertexTangent()
    {
        // Default: X = (1,0,0), Z = (0,0,1)
        TangentX = FPackedNormal(1.0f, 0.0f, 0.0f, 0.0f);
        TangentZ = FPackedNormal(0.0f, 0.0f, 1.0f, 1.0f); // W=1 for positive binormal
    }
    
    /**
     * Constructor from vectors
     * @param InTangentX Tangent vector
     * @param InTangentY Binormal vector (used to compute sign)
     * @param InTangentZ Normal vector
     */
    FORCEINLINE FStaticMeshVertexTangent(
        const Math::FVector3f& InTangentX,
        const Math::FVector3f& InTangentY,
        const Math::FVector3f& InTangentZ)
    {
        SetTangents(InTangentX, InTangentY, InTangentZ);
    }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    /**
     * Get the tangent vector (X axis)
     * @return Unpacked tangent vector
     */
    FORCEINLINE Math::FVector3f GetTangentX() const
    {
        return TangentX.ToFVector();
    }
    
    /**
     * Get the normal vector (Z axis)
     * @return Unpacked normal vector
     */
    FORCEINLINE Math::FVector3f GetTangentZ() const
    {
        return TangentZ.ToFVector();
    }
    
    /**
     * Get the normal as a 4D vector (includes binormal sign in W)
     * @return Unpacked normal with sign
     */
    FORCEINLINE Math::FVector4f GetTangentZ4() const
    {
        return TangentZ.ToFVector4f();
    }
    
    /**
     * Get the binormal vector (Y axis), computed from X and Z
     * @return Computed binormal vector
     */
    FORCEINLINE Math::FVector3f GetTangentY() const
    {
        return GenerateYAxis(TangentX, TangentZ);
    }
    
    /**
     * Set all tangent vectors
     * @param InTangentX Tangent vector
     * @param InTangentY Binormal vector
     * @param InTangentZ Normal vector
     */
    FORCEINLINE void SetTangents(
        const Math::FVector3f& InTangentX,
        const Math::FVector3f& InTangentY,
        const Math::FVector3f& InTangentZ)
    {
        TangentX = FPackedNormal(InTangentX);
        
        // Store normal with binormal sign in W
        float Sign = GetBasisDeterminantSign(InTangentX, InTangentY, InTangentZ);
        TangentZ = FPackedNormal(InTangentZ.X, InTangentZ.Y, InTangentZ.Z, Sign);
    }
};

/**
 * @struct FStaticMeshVertexTangentHighPrecision
 * @brief High precision tangent data (16-bit per component)
 * 
 * Same layout as FStaticMeshVertexTangent but with 16-bit precision.
 * Use when high-quality normal mapping is required.
 * 
 * Memory layout: [TangentX:8][TangentZ:8] = 16 bytes
 */
struct FStaticMeshVertexTangentHighPrecision
{
    /** Tangent vector (X axis of tangent space) */
    FPackedRGBA16N TangentX;
    
    /** Normal vector (Z axis of tangent space), W stores binormal sign */
    FPackedRGBA16N TangentZ;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    FORCEINLINE FStaticMeshVertexTangentHighPrecision()
    {
        TangentX = FPackedRGBA16N(1.0f, 0.0f, 0.0f, 0.0f);
        TangentZ = FPackedRGBA16N(0.0f, 0.0f, 1.0f, 1.0f);
    }
    
    FORCEINLINE FStaticMeshVertexTangentHighPrecision(
        const Math::FVector3f& InTangentX,
        const Math::FVector3f& InTangentY,
        const Math::FVector3f& InTangentZ)
    {
        SetTangents(InTangentX, InTangentY, InTangentZ);
    }
    
    // ========================================================================
    // Accessors
    // ========================================================================
    
    FORCEINLINE Math::FVector3f GetTangentX() const
    {
        return TangentX.ToFVector();
    }
    
    FORCEINLINE Math::FVector3f GetTangentZ() const
    {
        return TangentZ.ToFVector();
    }
    
    FORCEINLINE Math::FVector4f GetTangentZ4() const
    {
        return TangentZ.ToFVector4f();
    }
    
    FORCEINLINE Math::FVector3f GetTangentY() const
    {
        return GenerateYAxis(TangentX, TangentZ);
    }
    
    FORCEINLINE void SetTangents(
        const Math::FVector3f& InTangentX,
        const Math::FVector3f& InTangentY,
        const Math::FVector3f& InTangentZ)
    {
        TangentX = FPackedRGBA16N(InTangentX);
        float Sign = GetBasisDeterminantSign(InTangentX, InTangentY, InTangentZ);
        TangentZ = FPackedRGBA16N(InTangentZ.X, InTangentZ.Y, InTangentZ.Z, Sign);
    }
};

// ============================================================================
// UV Data Structures
// ============================================================================

/**
 * @struct FStaticMeshVertexUV
 * @brief Standard precision UV data (half-float)
 * 
 * Stores a single UV coordinate set in half-precision format.
 * 
 * Memory layout: [UV:4] = 4 bytes
 */
struct FStaticMeshVertexUV
{
    /** UV coordinates in half-precision */
    FVector2DHalf UV;
    
    FORCEINLINE FStaticMeshVertexUV()
    {
    }
    
    FORCEINLINE FStaticMeshVertexUV(const Math::FVector2f& InUV)
        : UV(InUV)
    {
    }
    
    FORCEINLINE Math::FVector2f GetUV() const
    {
        return UV.ToFVector2f();
    }
    
    FORCEINLINE void SetUV(const Math::FVector2f& InUV)
    {
        UV = FVector2DHalf(InUV);
    }
};

/**
 * @struct FStaticMeshVertexUVHighPrecision
 * @brief High precision UV data (full float)
 * 
 * Stores a single UV coordinate set in full precision.
 * Use when large UV ranges are needed.
 * 
 * Memory layout: [UV:8] = 8 bytes
 */
struct FStaticMeshVertexUVHighPrecision
{
    /** UV coordinates in full precision */
    Math::FVector2f UV;
    
    FORCEINLINE FStaticMeshVertexUVHighPrecision()
        : UV(0.0f, 0.0f)
    {
    }
    
    FORCEINLINE FStaticMeshVertexUVHighPrecision(const Math::FVector2f& InUV)
        : UV(InUV)
    {
    }
    
    FORCEINLINE Math::FVector2f GetUV() const
    {
        return UV;
    }
    
    FORCEINLINE void SetUV(const Math::FVector2f& InUV)
    {
        UV = InUV;
    }
};

// ============================================================================
// Complete Vertex Structures
// ============================================================================

/**
 * @struct FStaticMeshBuildVertex
 * @brief Full vertex data used during mesh building
 * 
 * This structure contains all vertex attributes in full precision.
 * Used during mesh construction before packing into GPU-friendly formats.
 * 
 * Reference UE5: FStaticMeshBuildVertex
 */
struct FStaticMeshBuildVertex
{
    /** Vertex position */
    Math::FVector3f Position;
    
    /** Tangent vector (X axis of tangent space) */
    Math::FVector3f TangentX;
    
    /** Binormal vector (Y axis of tangent space) */
    Math::FVector3f TangentY;
    
    /** Normal vector (Z axis of tangent space) */
    Math::FVector3f TangentZ;
    
    /** Texture coordinates (up to MAX_STATIC_MESH_TEXCOORDS sets) */
    Math::FVector2f UVs[MAX_STATIC_MESH_TEXCOORDS];
    
    /** Vertex color */
    FColor Color;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor - initializes to zero/default values
     */
    FORCEINLINE FStaticMeshBuildVertex()
        : Position(0.0f, 0.0f, 0.0f)
        , TangentX(1.0f, 0.0f, 0.0f)
        , TangentY(0.0f, 1.0f, 0.0f)
        , TangentZ(0.0f, 0.0f, 1.0f)
        , Color(255, 255, 255, 255)
    {
        // Initialize all UVs to zero
        for (uint32 i = 0; i < MAX_STATIC_MESH_TEXCOORDS; ++i)
        {
            UVs[i] = Math::FVector2f(0.0f, 0.0f);
        }
    }
    
    /**
     * Constructor with position only
     * @param InPosition Vertex position
     */
    explicit FORCEINLINE FStaticMeshBuildVertex(const Math::FVector3f& InPosition)
        : Position(InPosition)
        , TangentX(1.0f, 0.0f, 0.0f)
        , TangentY(0.0f, 1.0f, 0.0f)
        , TangentZ(0.0f, 0.0f, 1.0f)
        , Color(255, 255, 255, 255)
    {
        for (uint32 i = 0; i < MAX_STATIC_MESH_TEXCOORDS; ++i)
        {
            UVs[i] = Math::FVector2f(0.0f, 0.0f);
        }
    }
    
    /**
     * Constructor with position and UV
     * @param InPosition Vertex position
     * @param InUV Texture coordinate
     */
    FORCEINLINE FStaticMeshBuildVertex(const Math::FVector3f& InPosition, const Math::FVector2f& InUV)
        : Position(InPosition)
        , TangentX(1.0f, 0.0f, 0.0f)
        , TangentY(0.0f, 1.0f, 0.0f)
        , TangentZ(0.0f, 0.0f, 1.0f)
        , Color(255, 255, 255, 255)
    {
        UVs[0] = InUV;
        for (uint32 i = 1; i < MAX_STATIC_MESH_TEXCOORDS; ++i)
        {
            UVs[i] = Math::FVector2f(0.0f, 0.0f);
        }
    }
    
    /**
     * Full constructor
     * @param InPosition Vertex position
     * @param InNormal Normal vector
     * @param InTangent Tangent vector
     * @param InUV Texture coordinate
     * @param InColor Vertex color
     */
    FORCEINLINE FStaticMeshBuildVertex(
        const Math::FVector3f& InPosition,
        const Math::FVector3f& InNormal,
        const Math::FVector3f& InTangent,
        const Math::FVector2f& InUV,
        const FColor& InColor = FColor(255, 255, 255, 255))
        : Position(InPosition)
        , TangentX(InTangent)
        , TangentZ(InNormal)
        , Color(InColor)
    {
        // Compute binormal from normal and tangent
        ComputeBinormal();
        
        UVs[0] = InUV;
        for (uint32 i = 1; i < MAX_STATIC_MESH_TEXCOORDS; ++i)
        {
            UVs[i] = Math::FVector2f(0.0f, 0.0f);
        }
    }
    
    // ========================================================================
    // Helper Methods
    // ========================================================================
    
    /**
     * Compute binormal from tangent and normal
     * Assumes TangentX and TangentZ are already set
     */
    FORCEINLINE void ComputeBinormal()
    {
        // Y = Z x X (cross product)
        TangentY = Math::FVector3f(
            TangentZ.Y * TangentX.Z - TangentZ.Z * TangentX.Y,
            TangentZ.Z * TangentX.X - TangentZ.X * TangentX.Z,
            TangentZ.X * TangentX.Y - TangentZ.Y * TangentX.X
        );
        
        // Normalize
        float Length = std::sqrt(TangentY.X * TangentY.X + TangentY.Y * TangentY.Y + TangentY.Z * TangentY.Z);
        if (Length > 0.0001f)
        {
            TangentY.X /= Length;
            TangentY.Y /= Length;
            TangentY.Z /= Length;
        }
    }
    
    /**
     * Set tangent basis from normal only (generates arbitrary tangent)
     * @param InNormal The normal vector
     */
    FORCEINLINE void SetTangentBasisFromNormal(const Math::FVector3f& InNormal)
    {
        TangentZ = InNormal;
        
        // Generate arbitrary tangent perpendicular to normal
        // Use the axis most perpendicular to the normal
        Math::FVector3f Arbitrary;
        if (std::abs(InNormal.X) < 0.9f)
        {
            Arbitrary = Math::FVector3f(1.0f, 0.0f, 0.0f);
        }
        else
        {
            Arbitrary = Math::FVector3f(0.0f, 1.0f, 0.0f);
        }
        
        // TangentY = Normal x Arbitrary
        TangentY = Math::FVector3f(
            InNormal.Y * Arbitrary.Z - InNormal.Z * Arbitrary.Y,
            InNormal.Z * Arbitrary.X - InNormal.X * Arbitrary.Z,
            InNormal.X * Arbitrary.Y - InNormal.Y * Arbitrary.X
        );
        
        // Normalize TangentY
        float LengthY = std::sqrt(TangentY.X * TangentY.X + TangentY.Y * TangentY.Y + TangentY.Z * TangentY.Z);
        if (LengthY > 0.0001f)
        {
            TangentY.X /= LengthY;
            TangentY.Y /= LengthY;
            TangentY.Z /= LengthY;
        }
        
        // TangentX = TangentY x Normal
        TangentX = Math::FVector3f(
            TangentY.Y * InNormal.Z - TangentY.Z * InNormal.Y,
            TangentY.Z * InNormal.X - TangentY.X * InNormal.Z,
            TangentY.X * InNormal.Y - TangentY.Y * InNormal.X
        );
    }
    
    /**
     * Equality comparison (compares position only for welding)
     */
    FORCEINLINE bool operator==(const FStaticMeshBuildVertex& Other) const
    {
        return Position.X == Other.Position.X 
            && Position.Y == Other.Position.Y 
            && Position.Z == Other.Position.Z;
    }
};

/**
 * @struct FDynamicMeshVertex
 * @brief Vertex structure for dynamic mesh building (runtime generated meshes)
 * 
 * A complete vertex structure suitable for immediate rendering.
 * All data is packed into a single structure for simple vertex buffer creation.
 * 
 * Memory layout: [Position:12][TexCoord:8][TangentX:4][TangentZ:4][Color:4] = 32 bytes
 * 
 * Reference UE5: FDynamicMeshVertex
 */
struct FDynamicMeshVertex
{
    /** Vertex position */
    Math::FVector3f Position;
    
    /** Primary texture coordinate */
    Math::FVector2f TextureCoordinate;
    
    /** Tangent vector (packed) */
    FPackedNormal TangentX;
    
    /** Normal vector with binormal sign (packed) */
    FPackedNormal TangentZ;
    
    /** Vertex color */
    FColor Color;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor
     */
    FORCEINLINE FDynamicMeshVertex()
        : Position(0.0f, 0.0f, 0.0f)
        , TextureCoordinate(0.0f, 0.0f)
        , TangentX(1.0f, 0.0f, 0.0f, 0.0f)
        , TangentZ(0.0f, 0.0f, 1.0f, 1.0f)
        , Color(255, 255, 255, 255)
    {
    }
    
    /**
     * Constructor with position
     * @param InPosition Vertex position
     */
    explicit FORCEINLINE FDynamicMeshVertex(const Math::FVector3f& InPosition)
        : Position(InPosition)
        , TextureCoordinate(0.0f, 0.0f)
        , TangentX(1.0f, 0.0f, 0.0f, 0.0f)
        , TangentZ(0.0f, 0.0f, 1.0f, 1.0f)
        , Color(255, 255, 255, 255)
    {
    }
    
    /**
     * Constructor with position and UV
     * @param InPosition Vertex position
     * @param InTexCoord Texture coordinate
     */
    FORCEINLINE FDynamicMeshVertex(const Math::FVector3f& InPosition, const Math::FVector2f& InTexCoord)
        : Position(InPosition)
        , TextureCoordinate(InTexCoord)
        , TangentX(1.0f, 0.0f, 0.0f, 0.0f)
        , TangentZ(0.0f, 0.0f, 1.0f, 1.0f)
        , Color(255, 255, 255, 255)
    {
    }
    
    /**
     * Constructor with position, UV, and color
     * @param InPosition Vertex position
     * @param InTexCoord Texture coordinate
     * @param InColor Vertex color
     */
    FORCEINLINE FDynamicMeshVertex(
        const Math::FVector3f& InPosition,
        const Math::FVector2f& InTexCoord,
        const FColor& InColor)
        : Position(InPosition)
        , TextureCoordinate(InTexCoord)
        , TangentX(1.0f, 0.0f, 0.0f, 0.0f)
        , TangentZ(0.0f, 0.0f, 1.0f, 1.0f)
        , Color(InColor)
    {
    }
    
    /**
     * Full constructor
     * @param InPosition Vertex position
     * @param InTexCoord Texture coordinate
     * @param InTangentX Tangent vector
     * @param InTangentY Binormal vector
     * @param InTangentZ Normal vector
     * @param InColor Vertex color
     */
    FORCEINLINE FDynamicMeshVertex(
        const Math::FVector3f& InPosition,
        const Math::FVector2f& InTexCoord,
        const Math::FVector3f& InTangentX,
        const Math::FVector3f& InTangentY,
        const Math::FVector3f& InTangentZ,
        const FColor& InColor)
        : Position(InPosition)
        , TextureCoordinate(InTexCoord)
        , Color(InColor)
    {
        SetTangents(InTangentX, InTangentY, InTangentZ);
    }
    
    // ========================================================================
    // Tangent Methods
    // ========================================================================
    
    /**
     * Set tangent basis vectors
     * @param InTangentX Tangent vector
     * @param InTangentY Binormal vector
     * @param InTangentZ Normal vector
     */
    FORCEINLINE void SetTangents(
        const Math::FVector3f& InTangentX,
        const Math::FVector3f& InTangentY,
        const Math::FVector3f& InTangentZ)
    {
        TangentX = FPackedNormal(InTangentX);
        float Sign = GetBasisDeterminantSign(InTangentX, InTangentY, InTangentZ);
        TangentZ = FPackedNormal(InTangentZ.X, InTangentZ.Y, InTangentZ.Z, Sign);
    }
    
    /**
     * Get the binormal vector (computed from tangent and normal)
     * @return Binormal vector
     */
    FORCEINLINE Math::FVector3f GetTangentY() const
    {
        return GenerateYAxis(TangentX, TangentZ);
    }
    
    // ========================================================================
    // Vertex Declaration
    // ========================================================================
    
    /**
     * Get the vertex declaration for FDynamicMeshVertex
     * @return Vertex declaration describing this vertex format
     */
    static FVertexDeclaration GetVertexDeclaration()
    {
        return FVertexDeclaration({
            FVertexElement(0, offsetof(FDynamicMeshVertex, Position), 
                          EVertexElementType::Float3, EVertexElementSemantic::Position),
            FVertexElement(0, offsetof(FDynamicMeshVertex, TextureCoordinate), 
                          EVertexElementType::Float2, EVertexElementSemantic::TexCoord0),
            FVertexElement(0, offsetof(FDynamicMeshVertex, TangentX), 
                          EVertexElementType::PackedNormal, EVertexElementSemantic::Tangent),
            FVertexElement(0, offsetof(FDynamicMeshVertex, TangentZ), 
                          EVertexElementType::PackedNormal, EVertexElementSemantic::Normal),
            FVertexElement(0, offsetof(FDynamicMeshVertex, Color), 
                          EVertexElementType::UByte4N, EVertexElementSemantic::Color)
        });
    }
};

// ============================================================================
// Vertex Buffer Flags
// ============================================================================

/**
 * @struct FStaticMeshVertexBufferFlags
 * @brief Flags controlling vertex buffer creation and storage
 */
struct FStaticMeshVertexBufferFlags
{
    /** Whether the vertex data needs CPU access after GPU upload */
    bool bNeedsCPUAccess : 1;
    
    /** Whether to use high precision tangent basis (16-bit) */
    bool bUseHighPrecisionTangentBasis : 1;
    
    /** Whether to use high precision UVs (32-bit float) */
    bool bUseFullPrecisionUVs : 1;
    
    /** Whether backwards compatible F16 truncation should be used for UVs */
    bool bUseBackwardsCompatibleF16TruncUVs : 1;
    
    /**
     * Default constructor
     */
    FORCEINLINE FStaticMeshVertexBufferFlags()
        : bNeedsCPUAccess(false)
        , bUseHighPrecisionTangentBasis(false)
        , bUseFullPrecisionUVs(false)
        , bUseBackwardsCompatibleF16TruncUVs(false)
    {
    }
};

} // namespace MonsterEngine
