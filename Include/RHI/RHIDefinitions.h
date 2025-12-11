#pragma once

#include "Core/CoreMinimal.h"

namespace MonsterRender::RHI {
    
    // Resource usage flags
    enum class EResourceUsage : uint32 {
        None = 0,
        VertexBuffer = 1 << 0,
        IndexBuffer = 1 << 1,
        UniformBuffer = 1 << 2,
        StorageBuffer = 1 << 3,
        TransferSrc = 1 << 4,
        TransferDst = 1 << 5,
        RenderTarget = 1 << 6,
        DepthStencil = 1 << 7,
        ShaderResource = 1 << 8,
        UnorderedAccess = 1 << 9
    };
    
    // Enable bitwise operations for EResourceUsage
    inline constexpr EResourceUsage operator|(EResourceUsage lhs, EResourceUsage rhs) {
        return static_cast<EResourceUsage>(
            static_cast<uint32>(lhs) | static_cast<uint32>(rhs)
        );
    }
    
    inline constexpr EResourceUsage operator&(EResourceUsage lhs, EResourceUsage rhs) {
        return static_cast<EResourceUsage>(
            static_cast<uint32>(lhs) & static_cast<uint32>(rhs)
        );
    }
    
    inline constexpr EResourceUsage operator^(EResourceUsage lhs, EResourceUsage rhs) {
        return static_cast<EResourceUsage>(
            static_cast<uint32>(lhs) ^ static_cast<uint32>(rhs)
        );
    }
    
    inline constexpr EResourceUsage operator~(EResourceUsage rhs) {
        return static_cast<EResourceUsage>(~static_cast<uint32>(rhs));
    }
    
    inline constexpr EResourceUsage& operator|=(EResourceUsage& lhs, EResourceUsage rhs) {
        lhs = lhs | rhs;
        return lhs;
    }
    
    inline constexpr EResourceUsage& operator&=(EResourceUsage& lhs, EResourceUsage rhs) {
        lhs = lhs & rhs;
        return lhs;
    }
    
    inline constexpr EResourceUsage& operator^=(EResourceUsage& lhs, EResourceUsage rhs) {
        lhs = lhs ^ rhs;
        return lhs;
    }
    
    // Helper function to check if a specific usage flag is set
    inline constexpr bool hasResourceUsage(EResourceUsage usage, EResourceUsage flag) {
        return (usage & flag) != EResourceUsage::None;
    }
    
    // Memory usage hint (UE5-style)
    enum class EMemoryUsage : uint32 {
        Default,     // Device-local memory (GPU only)
        Upload,      // Host-visible, device-readable (CPU → GPU staging)
        Readback,    // Host-visible, device-writable (GPU → CPU readback)
        Dynamic      // Frequently updated by CPU
    };
    
    // Resource usage aliases for backward compatibility
    constexpr EResourceUsage CopySrc = EResourceUsage::TransferSrc;
    constexpr EResourceUsage CopyDst = EResourceUsage::TransferDst;
    
    /**
     * @enum EBufferUsageFlags
     * @brief Buffer usage flags (UE5-style)
     * 
     * Reference UE5: EBufferUsageFlags
     */
    enum class EBufferUsageFlags : uint32
    {
        None            = 0,
        
        /** The buffer will be written to once (immutable after creation) */
        Static          = 1 << 0,
        
        /** The buffer will be written to occasionally, GPU read only, CPU write only */
        Dynamic         = 1 << 1,
        
        /** The buffer's data will have a lifetime of one frame */
        Volatile        = 1 << 2,
        
        /** Allows an unordered access view to be created for the buffer */
        UnorderedAccess = 1 << 3,
        
        /** Create a byte address buffer */
        ByteAddressBuffer = 1 << 4,
        
        /** Buffer that the GPU will use as a source for a copy */
        SourceCopy      = 1 << 5,
        
        /** Create a buffer that can be bound as a stream output target */
        StreamOutput    = 1 << 6,
        
        /** Create a buffer which contains the arguments used by DispatchIndirect or DrawIndirect */
        DrawIndirect    = 1 << 7,
        
        /** Create a buffer that can be bound as a shader resource */
        ShaderResource  = 1 << 8,
        
        /** Request that this buffer is directly CPU accessible */
        KeepCPUAccessible = 1 << 9,
        
        /** Buffer should go in fast VRAM (hint only) */
        FastVRAM        = 1 << 10,
        
        /** Vertex buffer type */
        VertexBuffer    = 1 << 14,
        
        /** Index buffer type */
        IndexBuffer     = 1 << 15,
        
        /** Structured buffer type */
        StructuredBuffer = 1 << 16,
        
        // Helper bit-masks
        AnyDynamic = (Dynamic | Volatile),
    };
    
    // Enable bitwise operations for EBufferUsageFlags
    inline constexpr EBufferUsageFlags operator|(EBufferUsageFlags lhs, EBufferUsageFlags rhs) {
        return static_cast<EBufferUsageFlags>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));
    }
    
    inline constexpr EBufferUsageFlags operator&(EBufferUsageFlags lhs, EBufferUsageFlags rhs) {
        return static_cast<EBufferUsageFlags>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));
    }
    
    inline constexpr EBufferUsageFlags& operator|=(EBufferUsageFlags& lhs, EBufferUsageFlags rhs) {
        lhs = lhs | rhs;
        return lhs;
    }
    
    inline constexpr bool EnumHasAnyFlags(EBufferUsageFlags flags, EBufferUsageFlags contains) {
        return (flags & contains) != EBufferUsageFlags::None;
    }
    
    /**
     * @struct FRHIResourceCreateInfo
     * @brief Resource creation information (UE5-style)
     * 
     * Contains initial data and debug information for resource creation.
     * Reference UE5: FRHIResourceCreateInfo
     */
    struct FRHIResourceCreateInfo
    {
        /** Debug name for the resource */
        String DebugName;
        
        /** Initial data to upload to the resource (optional) */
        const void* BulkData = nullptr;
        
        /** Size of the initial data in bytes */
        uint32 BulkDataSize = 0;
        
        /** Constructor with debug name */
        FRHIResourceCreateInfo() = default;
        
        explicit FRHIResourceCreateInfo(const String& InDebugName)
            : DebugName(InDebugName)
        {}
        
        FRHIResourceCreateInfo(const String& InDebugName, const void* InData, uint32 InDataSize)
            : DebugName(InDebugName)
            , BulkData(InData)
            , BulkDataSize(InDataSize)
        {}
    };
    
    // Buffer description
    struct BufferDesc {
        uint32 size = 0;
        EResourceUsage usage = EResourceUsage::None;
        EMemoryUsage memoryUsage = EMemoryUsage::Default;
        bool cpuAccessible = false;
        String debugName;
        
        /** Stride for structured/vertex buffers */
        uint32 stride = 0;
        
        /** Initial data (optional) */
        const void* initialData = nullptr;
        uint32 initialDataSize = 0;
        
        BufferDesc() = default;
        BufferDesc(uint32 inSize, EResourceUsage inUsage, bool inCpuAccessible = false)
            : size(inSize), usage(inUsage), cpuAccessible(inCpuAccessible) {}
        
        /** Create a vertex buffer description */
        static BufferDesc VertexBuffer(uint32 inSize, uint32 inStride, bool inCpuAccessible = false) {
            BufferDesc desc;
            desc.size = inSize;
            desc.stride = inStride;
            desc.usage = EResourceUsage::VertexBuffer;
            desc.cpuAccessible = inCpuAccessible;
            return desc;
        }
        
        /** Create an index buffer description */
        static BufferDesc IndexBuffer(uint32 inSize, bool b32Bit, bool inCpuAccessible = false) {
            BufferDesc desc;
            desc.size = inSize;
            desc.stride = b32Bit ? 4 : 2;
            desc.usage = EResourceUsage::IndexBuffer;
            desc.cpuAccessible = inCpuAccessible;
            return desc;
        }
    };
    
    // Texture formats
    enum class EPixelFormat : uint32 {
        Unknown = 0,
        // 8-bit formats
        R8_UNORM,
        R8_SRGB,
        R8G8_UNORM,
        R8G8_SRGB,
        R8G8B8A8_UNORM,
        B8G8R8A8_UNORM,
        R8G8B8A8_SRGB,
        B8G8R8A8_SRGB,
        // Float formats
        R32G32B32A32_FLOAT,
        R32G32B32_FLOAT,
        R32G32_FLOAT,
        R32_FLOAT,
        // Depth formats
        D32_FLOAT,
        D24_UNORM_S8_UINT,
        D32_FLOAT_S8_UINT,
        D16_UNORM,
        // Compressed formats
        BC1_UNORM,
        BC1_SRGB,
        BC3_UNORM,
        BC3_SRGB
    };
    
    // Texture description
    struct TextureDesc {
        uint32 width = 1;
        uint32 height = 1;
        uint32 depth = 1;
        uint32 mipLevels = 1;
        uint32 arraySize = 1;
        EPixelFormat format = EPixelFormat::R8G8B8A8_UNORM;
        EResourceUsage usage = EResourceUsage::ShaderResource;
        String debugName;
        
        // Initial data for texture upload (optional)
        // If provided, texture will be initialized with this data
        const void* initialData = nullptr;
        uint32 initialDataSize = 0;
        
        TextureDesc() = default;
        TextureDesc(uint32 w, uint32 h, EPixelFormat fmt, EResourceUsage usg)
            : width(w), height(h), format(fmt), usage(usg) {}
    };
    
    // Primitive topology
    enum class EPrimitiveTopology : uint32 {
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip
    };
    
    // Blend state
    enum class EBlendOp : uint32 {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max
    };
    
    enum class EBlendFactor : uint32 {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DestColor,
        InvDestColor,
        DestAlpha,
        InvDestAlpha
    };
    
    struct BlendState {
        bool blendEnable = false;
        EBlendFactor srcColorBlend = EBlendFactor::One;
        EBlendFactor destColorBlend = EBlendFactor::Zero;
        EBlendOp colorBlendOp = EBlendOp::Add;
        EBlendFactor srcAlphaBlend = EBlendFactor::One;
        EBlendFactor destAlphaBlend = EBlendFactor::Zero;
        EBlendOp alphaBlendOp = EBlendOp::Add;
    };
    
    // Rasterizer state
    enum class EFillMode : uint32 {
        Solid,
        Wireframe
    };
    
    enum class ECullMode : uint32 {
        None,
        Front,
        Back
    };
    
    struct RasterizerState {
        EFillMode fillMode = EFillMode::Solid;
        ECullMode cullMode = ECullMode::Back;
        bool frontCounterClockwise = false;
        bool depthClampEnable = false;
        bool scissorEnable = false;
    };
    
    // Depth stencil state
    enum class EComparisonFunc : uint32 {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };
    
    // Alias for compatibility
    using ECompareOp = EComparisonFunc;
    
    struct DepthStencilState {
        bool depthEnable = true;
        bool depthWriteEnable = true;
        EComparisonFunc depthFunc = EComparisonFunc::Less;
        EComparisonFunc depthCompareOp = EComparisonFunc::Less;  // Alias for compatibility
        bool stencilEnable = false;
    };
    
    // Shader stage
    enum class EShaderStage : uint32 {
        Vertex = 1 << 0,
        Fragment = 1 << 1,
        Compute = 1 << 2,
        Geometry = 1 << 3,
        TessellationControl = 1 << 4,
        TessellationEvaluation = 1 << 5
    };
    
    // Vertex attribute format
    enum class EVertexFormat : uint32 {
        Float1,     // R32_SFLOAT
        Float2,     // R32G32_SFLOAT
        Float3,     // R32G32B32_SFLOAT
        Float4,     // R32G32B32A32_SFLOAT
        Int1,       // R32_SINT
        Int2,       // R32G32_SINT
        Int3,       // R32G32B32_SINT
        Int4,       // R32G32B32A32_SINT
        UInt1,      // R32_UINT
        UInt2,      // R32G32_UINT
        UInt3,      // R32G32B32_UINT
        UInt4,      // R32G32B32A32_UINT
        UByte4Norm  // R8G8B8A8_UNORM - for vertex colors
    };
    
    // Vertex attribute description
    struct VertexAttribute {
        uint32 location = 0;          // Shader location
        EVertexFormat format = EVertexFormat::Float3;
        uint32 offset = 0;            // Offset in bytes from start of vertex
        String semanticName;          // Optional semantic name (e.g., "POSITION", "TEXCOORD")
    };
    
    // Vertex input layout description
    struct VertexInputLayout {
        uint32 stride = 0;            // Size of one vertex in bytes
        TArray<VertexAttribute> attributes;
        
        // Helper to calculate stride from attributes
        static uint32 calculateStride(const TArray<VertexAttribute>& attrs) {
            uint32 maxEnd = 0;
            for (const auto& attr : attrs) {
                uint32 size = 0;
                switch (attr.format) {
                    case EVertexFormat::Float1: case EVertexFormat::Int1: case EVertexFormat::UInt1: size = 4; break;
                    case EVertexFormat::Float2: case EVertexFormat::Int2: case EVertexFormat::UInt2: size = 8; break;
                    case EVertexFormat::Float3: case EVertexFormat::Int3: case EVertexFormat::UInt3: size = 12; break;
                    case EVertexFormat::Float4: case EVertexFormat::Int4: case EVertexFormat::UInt4: size = 16; break;
                }
                maxEnd = (std::max)(maxEnd, attr.offset + size);
            }
            return maxEnd;
        }
    };
    
    // Pipeline state description
    struct PipelineStateDesc {
        TSharedPtr<class IRHIVertexShader> vertexShader;
        TSharedPtr<class IRHIPixelShader> pixelShader;
        EPrimitiveTopology primitiveTopology = EPrimitiveTopology::TriangleList;
        BlendState blendState;
        RasterizerState rasterizerState;
        DepthStencilState depthStencilState;
        TArray<EPixelFormat> renderTargetFormats;
        EPixelFormat depthStencilFormat = EPixelFormat::Unknown;
        VertexInputLayout vertexLayout;  // Vertex input layout description
        String debugName;
    };
    
    // Viewport
    struct Viewport {
        float32 x = 0.0f;
        float32 y = 0.0f;
        float32 width = 0.0f;
        float32 height = 0.0f;
        float32 minDepth = 0.0f;
        float32 maxDepth = 1.0f;
        
        Viewport() = default;
        Viewport(float32 w, float32 h) : width(w), height(h) {}
        Viewport(float32 inX, float32 inY, float32 w, float32 h) 
            : x(inX), y(inY), width(w), height(h) {}
    };
    
    // Scissor rect
    struct ScissorRect {
        int32 left = 0;
        int32 top = 0;
        int32 right = 0;
        int32 bottom = 0;
        
        ScissorRect() = default;
        ScissorRect(int32 w, int32 h) : right(w), bottom(h) {}
        ScissorRect(int32 l, int32 t, int32 r, int32 b) 
            : left(l), top(t), right(r), bottom(b) {}
    };
}
