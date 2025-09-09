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
    
    // Buffer description
    struct BufferDesc {
        uint32 size = 0;
        EResourceUsage usage = EResourceUsage::None;
        bool cpuAccessible = false;
        String debugName;
        
        BufferDesc() = default;
        BufferDesc(uint32 inSize, EResourceUsage inUsage, bool inCpuAccessible = false)
            : size(inSize), usage(inUsage), cpuAccessible(inCpuAccessible) {}
    };
    
    // Texture formats
    enum class EPixelFormat : uint32 {
        Unknown = 0,
        R8G8B8A8_UNORM,
        B8G8R8A8_UNORM,
        R8G8B8A8_SRGB,
        B8G8R8A8_SRGB,
        R32G32B32A32_FLOAT,
        R32G32B32_FLOAT,
        R32G32_FLOAT,
        R32_FLOAT,
        D32_FLOAT,
        D24_UNORM_S8_UINT,
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
    
    struct DepthStencilState {
        bool depthEnable = true;
        bool depthWriteEnable = true;
        EComparisonFunc depthFunc = EComparisonFunc::Less;
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
