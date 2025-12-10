// Copyright Monster Engine. All Rights Reserved.
// Reference UE5: Engine/Source/Runtime/RenderCore/Public/VertexFactory.h

#pragma once

/**
 * @file VertexFactory.h
 * @brief Vertex element, declaration, and factory definitions
 * 
 * This file defines the vertex input layout system following UE5's architecture:
 * - FVertexElement: Describes a single vertex attribute
 * - FVertexDeclaration: Collection of vertex elements forming a complete layout
 * - EVertexElementType: Supported vertex attribute data types
 * - EVertexElementSemantic: Semantic meaning of vertex attributes
 * 
 * The vertex factory system abstracts the vertex input layout from the
 * rendering pipeline, allowing flexible vertex formats for different mesh types.
 */

#include "Core/CoreTypes.h"
#include "Containers/Array.h"
#include "Core/Templates/SharedPointer.h"

namespace MonsterEngine
{

// ============================================================================
// Vertex Element Type Enumeration
// ============================================================================

/**
 * @enum EVertexElementType
 * @brief Supported vertex attribute data types
 * 
 * Defines all supported data formats for vertex attributes.
 * These map directly to GPU vertex input formats.
 * 
 * Reference UE5: EVertexElementType
 */
enum class EVertexElementType : uint8
{
    None = 0,
    
    // Float types (32-bit per component)
    Float1,         // R32_FLOAT - Single float
    Float2,         // R32G32_FLOAT - 2D vector
    Float3,         // R32G32B32_FLOAT - 3D vector
    Float4,         // R32G32B32A32_FLOAT - 4D vector
    
    // Half-float types (16-bit per component)
    Half2,          // R16G16_FLOAT - 2D half-precision
    Half4,          // R16G16B16A16_FLOAT - 4D half-precision
    
    // Packed normal types
    PackedNormal,   // R8G8B8A8_SNORM - 8-bit signed normalized (FPackedNormal)
    Short2,         // R16G16_SNORM - 16-bit signed normalized
    Short4,         // R16G16B16A16_SNORM - 16-bit signed normalized (FPackedRGBA16N)
    Short2N,        // R16G16_SNORM - Normalized
    Short4N,        // R16G16B16A16_SNORM - Normalized
    
    // Unsigned byte types
    UByte4,         // R8G8B8A8_UINT - 4 unsigned bytes
    UByte4N,        // R8G8B8A8_UNORM - 4 unsigned bytes normalized (FColor)
    
    // Integer types
    UInt,           // R32_UINT - Single unsigned int
    Int4,           // R32G32B32A32_SINT - 4 signed ints
    
    // Special types
    Color,          // B8G8R8A8_UNORM - BGRA color (FColor layout)
    
    MAX
};

/**
 * Get the size in bytes of a vertex element type
 * @param Type The vertex element type
 * @return Size in bytes
 */
FORCEINLINE uint32 GetVertexElementTypeSize(EVertexElementType Type)
{
    switch (Type)
    {
        case EVertexElementType::Float1:        return 4;
        case EVertexElementType::Float2:        return 8;
        case EVertexElementType::Float3:        return 12;
        case EVertexElementType::Float4:        return 16;
        case EVertexElementType::Half2:         return 4;
        case EVertexElementType::Half4:         return 8;
        case EVertexElementType::PackedNormal:  return 4;
        case EVertexElementType::Short2:        return 4;
        case EVertexElementType::Short4:        return 8;
        case EVertexElementType::Short2N:       return 4;
        case EVertexElementType::Short4N:       return 8;
        case EVertexElementType::UByte4:        return 4;
        case EVertexElementType::UByte4N:       return 4;
        case EVertexElementType::UInt:          return 4;
        case EVertexElementType::Int4:          return 16;
        case EVertexElementType::Color:         return 4;
        default:                                return 0;
    }
}

// ============================================================================
// Vertex Element Semantic Enumeration
// ============================================================================

/**
 * @enum EVertexElementSemantic
 * @brief Semantic meaning of vertex attributes
 * 
 * Defines the semantic purpose of each vertex attribute.
 * Used for automatic binding in shaders and validation.
 * 
 * Reference UE5: Uses string-based semantics, we use enum for efficiency
 */
enum class EVertexElementSemantic : uint8
{
    None = 0,
    
    Position,           // Vertex position (POSITION)
    Normal,             // Vertex normal (NORMAL)
    Tangent,            // Vertex tangent (TANGENT)
    Binormal,           // Vertex binormal/bitangent (BINORMAL)
    Color,              // Vertex color (COLOR)
    
    TexCoord0,          // Texture coordinate 0 (TEXCOORD0)
    TexCoord1,          // Texture coordinate 1 (TEXCOORD1)
    TexCoord2,          // Texture coordinate 2 (TEXCOORD2)
    TexCoord3,          // Texture coordinate 3 (TEXCOORD3)
    TexCoord4,          // Texture coordinate 4 (TEXCOORD4)
    TexCoord5,          // Texture coordinate 5 (TEXCOORD5)
    TexCoord6,          // Texture coordinate 6 (TEXCOORD6)
    TexCoord7,          // Texture coordinate 7 (TEXCOORD7)
    
    BlendWeight,        // Skinning blend weights (BLENDWEIGHT)
    BlendIndices,       // Skinning blend indices (BLENDINDICES)
    
    InstanceTransform0, // Instance transform row 0
    InstanceTransform1, // Instance transform row 1
    InstanceTransform2, // Instance transform row 2
    InstanceTransform3, // Instance transform row 3
    
    MAX
};

/**
 * Get the string name of a vertex element semantic
 * @param Semantic The semantic enum value
 * @return String representation for shader binding
 */
inline const char* GetVertexElementSemanticName(EVertexElementSemantic Semantic)
{
    switch (Semantic)
    {
        case EVertexElementSemantic::Position:          return "POSITION";
        case EVertexElementSemantic::Normal:            return "NORMAL";
        case EVertexElementSemantic::Tangent:           return "TANGENT";
        case EVertexElementSemantic::Binormal:          return "BINORMAL";
        case EVertexElementSemantic::Color:             return "COLOR";
        case EVertexElementSemantic::TexCoord0:         return "TEXCOORD0";
        case EVertexElementSemantic::TexCoord1:         return "TEXCOORD1";
        case EVertexElementSemantic::TexCoord2:         return "TEXCOORD2";
        case EVertexElementSemantic::TexCoord3:         return "TEXCOORD3";
        case EVertexElementSemantic::TexCoord4:         return "TEXCOORD4";
        case EVertexElementSemantic::TexCoord5:         return "TEXCOORD5";
        case EVertexElementSemantic::TexCoord6:         return "TEXCOORD6";
        case EVertexElementSemantic::TexCoord7:         return "TEXCOORD7";
        case EVertexElementSemantic::BlendWeight:       return "BLENDWEIGHT";
        case EVertexElementSemantic::BlendIndices:      return "BLENDINDICES";
        case EVertexElementSemantic::InstanceTransform0: return "INSTANCE_TRANSFORM0";
        case EVertexElementSemantic::InstanceTransform1: return "INSTANCE_TRANSFORM1";
        case EVertexElementSemantic::InstanceTransform2: return "INSTANCE_TRANSFORM2";
        case EVertexElementSemantic::InstanceTransform3: return "INSTANCE_TRANSFORM3";
        default:                                        return "UNKNOWN";
    }
}

// ============================================================================
// Vertex Element Structure
// ============================================================================

/**
 * @struct FVertexElement
 * @brief Describes a single vertex attribute in a vertex buffer
 * 
 * Each vertex element defines:
 * - Stream index: Which vertex buffer stream this element comes from
 * - Offset: Byte offset within the vertex
 * - Type: Data format of the element
 * - Semantic: What the element represents (position, normal, etc.)
 * - Semantic index: For multiple elements with same semantic (e.g., TEXCOORD0, TEXCOORD1)
 * 
 * Reference UE5: FVertexElement
 */
struct FVertexElement
{
    /** Vertex buffer stream index (for multi-stream vertex layouts) */
    uint8 StreamIndex;
    
    /** Byte offset from the start of the vertex to this element */
    uint16 Offset;
    
    /** Data type of this element */
    EVertexElementType Type;
    
    /** Semantic meaning of this element */
    EVertexElementSemantic Semantic;
    
    /** Semantic index for multiple elements with same semantic */
    uint8 SemanticIndex;
    
    /** Whether this element uses per-instance data (for instancing) */
    bool bUseInstanceIndex;
    
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor
     */
    FORCEINLINE FVertexElement()
        : StreamIndex(0)
        , Offset(0)
        , Type(EVertexElementType::None)
        , Semantic(EVertexElementSemantic::None)
        , SemanticIndex(0)
        , bUseInstanceIndex(false)
    {
    }
    
    /**
     * Full constructor
     * @param InStreamIndex Vertex buffer stream index
     * @param InOffset Byte offset in vertex
     * @param InType Data type
     * @param InSemantic Semantic meaning
     * @param InSemanticIndex Semantic index (default 0)
     * @param bInUseInstanceIndex Whether this is per-instance data
     */
    FORCEINLINE FVertexElement(
        uint8 InStreamIndex,
        uint16 InOffset,
        EVertexElementType InType,
        EVertexElementSemantic InSemantic,
        uint8 InSemanticIndex = 0,
        bool bInUseInstanceIndex = false)
        : StreamIndex(InStreamIndex)
        , Offset(InOffset)
        , Type(InType)
        , Semantic(InSemantic)
        , SemanticIndex(InSemanticIndex)
        , bUseInstanceIndex(bInUseInstanceIndex)
    {
    }
    
    /**
     * Get the size of this element in bytes
     * @return Size in bytes
     */
    FORCEINLINE uint32 GetSize() const
    {
        return GetVertexElementTypeSize(Type);
    }
    
    /**
     * Equality comparison
     */
    FORCEINLINE bool operator==(const FVertexElement& Other) const
    {
        return StreamIndex == Other.StreamIndex
            && Offset == Other.Offset
            && Type == Other.Type
            && Semantic == Other.Semantic
            && SemanticIndex == Other.SemanticIndex
            && bUseInstanceIndex == Other.bUseInstanceIndex;
    }
    
    FORCEINLINE bool operator!=(const FVertexElement& Other) const
    {
        return !(*this == Other);
    }
};

// ============================================================================
// Vertex Declaration
// ============================================================================

/**
 * @class FVertexDeclaration
 * @brief Collection of vertex elements forming a complete vertex layout
 * 
 * A vertex declaration describes the complete layout of vertex data,
 * including all attributes and their formats. This is used to create
 * the GPU input layout for rendering.
 * 
 * Reference UE5: FVertexDeclarationRHI
 */
class FVertexDeclaration
{
public:
    // ========================================================================
    // Constructors
    // ========================================================================
    
    /**
     * Default constructor - creates empty declaration
     */
    FVertexDeclaration() = default;
    
    /**
     * Constructor from element array
     * @param InElements Array of vertex elements
     */
    explicit FVertexDeclaration(const TArray<FVertexElement>& InElements)
        : Elements(InElements)
    {
        CalculateStrides();
    }
    
    /**
     * Constructor from initializer list
     * @param InElements Initializer list of vertex elements
     */
    FVertexDeclaration(std::initializer_list<FVertexElement> InElements)
        : Elements(InElements)
    {
        CalculateStrides();
    }
    
    // ========================================================================
    // Element Management
    // ========================================================================
    
    /**
     * Add a vertex element to the declaration
     * @param Element The element to add
     */
    void AddElement(const FVertexElement& Element)
    {
        Elements.Add(Element);
        CalculateStrides();
    }
    
    /**
     * Add a vertex element with parameters
     * @param StreamIndex Vertex buffer stream index
     * @param Offset Byte offset in vertex
     * @param Type Data type
     * @param Semantic Semantic meaning
     * @param SemanticIndex Semantic index (default 0)
     */
    void AddElement(
        uint8 StreamIndex,
        uint16 Offset,
        EVertexElementType Type,
        EVertexElementSemantic Semantic,
        uint8 SemanticIndex = 0)
    {
        Elements.Add(FVertexElement(StreamIndex, Offset, Type, Semantic, SemanticIndex));
        CalculateStrides();
    }
    
    /**
     * Get all elements in the declaration
     * @return Reference to element array
     */
    const TArray<FVertexElement>& GetElements() const
    {
        return Elements;
    }
    
    /**
     * Get the number of elements
     * @return Element count
     */
    int32 GetNumElements() const
    {
        return Elements.Num();
    }
    
    /**
     * Find an element by semantic
     * @param Semantic The semantic to search for
     * @param SemanticIndex The semantic index (default 0)
     * @return Pointer to element or nullptr if not found
     */
    const FVertexElement* FindElement(EVertexElementSemantic Semantic, uint8 SemanticIndex = 0) const
    {
        for (const FVertexElement& Element : Elements)
        {
            if (Element.Semantic == Semantic && Element.SemanticIndex == SemanticIndex)
            {
                return &Element;
            }
        }
        return nullptr;
    }
    
    // ========================================================================
    // Stride Information
    // ========================================================================
    
    /**
     * Get the stride for a specific stream
     * @param StreamIndex The stream index
     * @return Stride in bytes, or 0 if stream not used
     */
    uint32 GetStride(uint8 StreamIndex = 0) const
    {
        if (StreamIndex < static_cast<uint8>(StreamStrides.Num()))
        {
            return StreamStrides[StreamIndex];
        }
        return 0;
    }
    
    /**
     * Get all stream strides
     * @return Array of strides per stream
     */
    const TArray<uint32>& GetStrides() const
    {
        return StreamStrides;
    }
    
    /**
     * Get the number of streams used
     * @return Number of unique stream indices
     */
    int32 GetNumStreams() const
    {
        return StreamStrides.Num();
    }
    
    // ========================================================================
    // Validation
    // ========================================================================
    
    /**
     * Check if the declaration has a position element
     * @return true if position element exists
     */
    bool HasPosition() const
    {
        return FindElement(EVertexElementSemantic::Position) != nullptr;
    }
    
    /**
     * Check if the declaration has normal element
     * @return true if normal element exists
     */
    bool HasNormal() const
    {
        return FindElement(EVertexElementSemantic::Normal) != nullptr;
    }
    
    /**
     * Check if the declaration has tangent element
     * @return true if tangent element exists
     */
    bool HasTangent() const
    {
        return FindElement(EVertexElementSemantic::Tangent) != nullptr;
    }
    
    /**
     * Check if the declaration has texture coordinates
     * @param Index UV channel index (0-7)
     * @return true if texcoord element exists
     */
    bool HasTexCoord(uint8 Index = 0) const
    {
        EVertexElementSemantic Semantic = static_cast<EVertexElementSemantic>(
            static_cast<uint8>(EVertexElementSemantic::TexCoord0) + Index);
        return FindElement(Semantic) != nullptr;
    }
    
    /**
     * Check if the declaration has vertex color
     * @return true if color element exists
     */
    bool HasColor() const
    {
        return FindElement(EVertexElementSemantic::Color) != nullptr;
    }
    
    /**
     * Validate the declaration for completeness
     * @return true if declaration is valid for rendering
     */
    bool IsValid() const
    {
        // At minimum, we need a position
        return HasPosition() && Elements.Num() > 0;
    }
    
    // ========================================================================
    // Comparison
    // ========================================================================
    
    bool operator==(const FVertexDeclaration& Other) const
    {
        if (Elements.Num() != Other.Elements.Num())
        {
            return false;
        }
        
        for (int32 i = 0; i < Elements.Num(); ++i)
        {
            if (Elements[i] != Other.Elements[i])
            {
                return false;
            }
        }
        
        return true;
    }
    
    bool operator!=(const FVertexDeclaration& Other) const
    {
        return !(*this == Other);
    }

private:
    /** Array of vertex elements */
    TArray<FVertexElement> Elements;
    
    /** Calculated stride for each stream */
    TArray<uint32> StreamStrides;
    
    /**
     * Calculate strides for all streams based on elements
     */
    void CalculateStrides()
    {
        StreamStrides.Empty();
        
        // Find max stream index
        uint8 MaxStream = 0;
        for (const FVertexElement& Element : Elements)
        {
            if (Element.StreamIndex > MaxStream)
            {
                MaxStream = Element.StreamIndex;
            }
        }
        
        // Resize and initialize strides
        StreamStrides.SetNum(MaxStream + 1);
        for (int32 i = 0; i <= MaxStream; ++i)
        {
            StreamStrides[i] = 0;
        }
        
        // Calculate stride for each stream (max offset + size)
        for (const FVertexElement& Element : Elements)
        {
            uint32 EndOffset = Element.Offset + Element.GetSize();
            if (EndOffset > StreamStrides[Element.StreamIndex])
            {
                StreamStrides[Element.StreamIndex] = EndOffset;
            }
        }
    }
};

// ============================================================================
// Common Vertex Declarations
// ============================================================================

/**
 * @namespace VertexDeclarations
 * @brief Predefined vertex declarations for common use cases
 */
namespace VertexDeclarations
{
    /**
     * Get a simple position-only vertex declaration
     * Layout: Position (Float3)
     * Stride: 12 bytes
     */
    inline FVertexDeclaration GetPositionOnly()
    {
        return FVertexDeclaration({
            FVertexElement(0, 0, EVertexElementType::Float3, EVertexElementSemantic::Position)
        });
    }
    
    /**
     * Get a position + normal vertex declaration
     * Layout: Position (Float3), Normal (PackedNormal)
     * Stride: 16 bytes
     */
    inline FVertexDeclaration GetPositionNormal()
    {
        return FVertexDeclaration({
            FVertexElement(0, 0, EVertexElementType::Float3, EVertexElementSemantic::Position),
            FVertexElement(0, 12, EVertexElementType::PackedNormal, EVertexElementSemantic::Normal)
        });
    }
    
    /**
     * Get a position + texcoord vertex declaration
     * Layout: Position (Float3), TexCoord0 (Float2)
     * Stride: 20 bytes
     */
    inline FVertexDeclaration GetPositionTexCoord()
    {
        return FVertexDeclaration({
            FVertexElement(0, 0, EVertexElementType::Float3, EVertexElementSemantic::Position),
            FVertexElement(0, 12, EVertexElementType::Float2, EVertexElementSemantic::TexCoord0)
        });
    }
    
    /**
     * Get a standard static mesh vertex declaration
     * Layout: Position (Float3), Normal (PackedNormal), Tangent (PackedNormal), 
     *         TexCoord0 (Half2), Color (UByte4N)
     * Stride: 28 bytes
     */
    inline FVertexDeclaration GetStaticMesh()
    {
        return FVertexDeclaration({
            FVertexElement(0, 0, EVertexElementType::Float3, EVertexElementSemantic::Position),
            FVertexElement(0, 12, EVertexElementType::PackedNormal, EVertexElementSemantic::Normal),
            FVertexElement(0, 16, EVertexElementType::PackedNormal, EVertexElementSemantic::Tangent),
            FVertexElement(0, 20, EVertexElementType::Half2, EVertexElementSemantic::TexCoord0),
            FVertexElement(0, 24, EVertexElementType::UByte4N, EVertexElementSemantic::Color)
        });
    }
    
    /**
     * Get a high precision static mesh vertex declaration
     * Layout: Position (Float3), Normal (Short4N), Tangent (Short4N),
     *         TexCoord0 (Float2), TexCoord1 (Float2), Color (UByte4N)
     * Stride: 48 bytes
     */
    inline FVertexDeclaration GetStaticMeshHighPrecision()
    {
        return FVertexDeclaration({
            FVertexElement(0, 0, EVertexElementType::Float3, EVertexElementSemantic::Position),
            FVertexElement(0, 12, EVertexElementType::Short4N, EVertexElementSemantic::Normal),
            FVertexElement(0, 20, EVertexElementType::Short4N, EVertexElementSemantic::Tangent),
            FVertexElement(0, 28, EVertexElementType::Float2, EVertexElementSemantic::TexCoord0),
            FVertexElement(0, 36, EVertexElementType::Float2, EVertexElementSemantic::TexCoord1),
            FVertexElement(0, 44, EVertexElementType::UByte4N, EVertexElementSemantic::Color)
        });
    }
}

} // namespace MonsterEngine
