#pragma once

#include "Core/CoreMinimal.h"
#include "RDG/RDGDefinitions.h"
#include "RDG/RDGResource.h"
#include "RHI/IRHICommandList.h"

/**
 * RDG Pass definitions
 * Reference: UE5 RenderGraphPass.h
 */

// Forward declarations for parallel rendering support
namespace MonsterEngine {
    class FGraphEvent;
    using FGraphEventRef = TSharedPtr<FGraphEvent>;
}

namespace MonsterRender {
namespace RDG {

// Import commonly used types into RDG namespace
using MonsterEngine::FString;
using MonsterEngine::TArray;
using MonsterEngine::TFunction;
using MonsterEngine::FGraphEventRef;

// Forward declarations
class FRDGBuilder;

/**
 * Resource access record for tracking read/write dependencies
 */
struct FRDGResourceAccess
{
    FRDGResourceRef resource = nullptr;
    ERHIAccess access = ERHIAccess::Unknown;
    
    FRDGResourceAccess() = default;
    FRDGResourceAccess(FRDGResourceRef inResource, ERHIAccess inAccess)
        : resource(inResource)
        , access(inAccess)
    {}
};

/**
 * Texture access record with subresource information
 */
struct FRDGTextureAccess
{
    FRDGTextureRef texture = nullptr;
    ERHIAccess access = ERHIAccess::Unknown;
    
    // Subresource range (0xFFFFFFFF means all subresources)
    uint32 mipLevel = 0xFFFFFFFF;
    uint32 arraySlice = 0xFFFFFFFF;
    
    FRDGTextureAccess() = default;
    
    FRDGTextureAccess(FRDGTextureRef inTexture, ERHIAccess inAccess)
        : texture(inTexture)
        , access(inAccess)
    {}
    
    FRDGTextureAccess(FRDGTextureRef inTexture, ERHIAccess inAccess, 
                      uint32 inMipLevel, uint32 inArraySlice)
        : texture(inTexture)
        , access(inAccess)
        , mipLevel(inMipLevel)
        , arraySlice(inArraySlice)
    {}
    
    // Check if this access affects all subresources
    bool isWholeResource() const
    {
        return mipLevel == 0xFFFFFFFF && arraySlice == 0xFFFFFFFF;
    }
};

/**
 * Buffer access record
 */
struct FRDGBufferAccess
{
    FRDGBufferRef buffer = nullptr;
    ERHIAccess access = ERHIAccess::Unknown;
    
    FRDGBufferAccess() = default;
    FRDGBufferAccess(FRDGBufferRef inBuffer, ERHIAccess inAccess)
        : buffer(inBuffer)
        , access(inAccess)
    {}
};

/**
 * Pass parameter builder for declaring resource dependencies
 * Reference: UE5 FRDGPassBuilder (part of pass setup lambda)
 */
class FRDGPassBuilder
{
public:
    FRDGPassBuilder() = default;
    
    // Texture access declarations
    void readTexture(FRDGTextureRef texture, ERHIAccess access = ERHIAccess::SRVGraphics)
    {
        m_textureAccesses.Add(FRDGTextureAccess(texture, access));
    }
    
    void writeTexture(FRDGTextureRef texture, ERHIAccess access = ERHIAccess::RTV)
    {
        m_textureAccesses.Add(FRDGTextureAccess(texture, access));
    }
    
    void writeDepth(FRDGTextureRef depthTexture, ERHIAccess access = ERHIAccess::DSVWrite)
    {
        m_textureAccesses.Add(FRDGTextureAccess(depthTexture, access));
    }
    
    void readDepth(FRDGTextureRef depthTexture, ERHIAccess access = ERHIAccess::DSVRead)
    {
        m_textureAccesses.Add(FRDGTextureAccess(depthTexture, access));
    }
    
    // Buffer access declarations
    void readBuffer(FRDGBufferRef buffer, ERHIAccess access = ERHIAccess::SRVGraphics)
    {
        m_bufferAccesses.Add(FRDGBufferAccess(buffer, access));
    }
    
    void writeBuffer(FRDGBufferRef buffer, ERHIAccess access = ERHIAccess::UAVGraphics)
    {
        m_bufferAccesses.Add(FRDGBufferAccess(buffer, access));
    }
    
    // Get recorded accesses
    const TArray<FRDGTextureAccess>& getTextureAccesses() const { return m_textureAccesses; }
    const TArray<FRDGBufferAccess>& getBufferAccesses() const { return m_bufferAccesses; }
    
private:
    TArray<FRDGTextureAccess> m_textureAccesses;
    TArray<FRDGBufferAccess> m_bufferAccesses;
};

/**
 * RDG Pass represents a single rendering operation
 * Reference: UE5 FRDGPass
 */
class FRDGPass
{
public:
    FRDGPass(const FString& inName, ERDGPassFlags inFlags)
        : m_name(inName)
        , m_flags(inFlags)
    {}
    
    virtual ~FRDGPass() = default;
    
    // Get pass name
    const FString& getName() const { return m_name; }
    
    // Get pass flags
    ERDGPassFlags getFlags() const { return m_flags; }
    
    // Get pass handle
    FRDGPassHandle getHandle() const { return m_handle; }
    
    // Check if pass is culled
    bool isCulled() const { return m_bCulled; }
    
    // Get resource accesses
    const TArray<FRDGTextureAccess>& getTextureAccesses() const { return m_textureAccesses; }
    const TArray<FRDGBufferAccess>& getBufferAccesses() const { return m_bufferAccesses; }
    
    // Execute the pass
    virtual void execute(RHI::IRHICommandList& rhiCmdList) = 0;
    
    // Check if this pass supports parallel execution
    virtual bool isParallel() const { return false; }
    
protected:
    FString m_name;
    ERDGPassFlags m_flags;
    FRDGPassHandle m_handle;
    bool m_bCulled = false;
    
    // Resource dependencies
    TArray<FRDGTextureAccess> m_textureAccesses;
    TArray<FRDGBufferAccess> m_bufferAccesses;
    
    // Pass dependency tracking for topological sort
    TArray<FRDGPassHandle> m_dependencies;  // Passes this pass depends on
    TArray<FRDGPassHandle> m_dependents;    // Passes that depend on this pass
    
    friend class FRDGBuilder;
};

/**
 * Lambda-based pass implementation
 * Stores a user-provided lambda for execution
 */
template<typename ExecuteLambdaType>
class TRDGLambdaPass : public FRDGPass
{
public:
    TRDGLambdaPass(const FString& inName, 
                   ERDGPassFlags inFlags,
                   ExecuteLambdaType&& inExecuteLambda)
        : FRDGPass(inName, inFlags)
        , m_executeLambda(std::move(inExecuteLambda))
    {}
    
    virtual void execute(RHI::IRHICommandList& rhiCmdList) override
    {
        m_executeLambda(rhiCmdList);
    }
    
    virtual bool isParallel() const override { return false; }
    
private:
    ExecuteLambdaType m_executeLambda;
};

/**
 * Parallel pass implementation
 * Executes multiple sub-passes in parallel using FTaskGraph
 * 
 * Usage:
 * builder.AddParallelPass("ParallelGeometry", ERDGPassFlags::Raster,
 *     [&](FRDGPassBuilder& builder) {
 *         builder.WriteTexture(colorTarget);
 *         builder.WriteDepth(depthTarget);
 *     },
 *     [=](RHI::IRHICommandList& cmdList, TArray<MonsterEngine::FGraphEventRef>& outTasks) {
 *         // Dispatch parallel tasks
 *         outTasks.Add(dispatchBasePass());
 *         outTasks.Add(dispatchPBRPass());
 *     });
 */
template<typename ParallelExecuteLambdaType>
class TRDGParallelPass : public FRDGPass
{
public:
    TRDGParallelPass(const FString& inName,
                     ERDGPassFlags inFlags,
                     ParallelExecuteLambdaType&& inExecuteLambda)
        : FRDGPass(inName, inFlags)
        , m_executeLambda(std::move(inExecuteLambda))
    {}
    
    virtual void execute(RHI::IRHICommandList& rhiCmdList) override
    {
        // Execute parallel dispatch lambda
        TArray<MonsterEngine::FGraphEventRef> parallelTasks;
        m_executeLambda(rhiCmdList, parallelTasks);
        
        // Wait for all parallel tasks to complete
        for (auto& task : parallelTasks)
        {
            if (task)
            {
                task->Wait();
            }
        }
    }
    
    virtual bool isParallel() const override { return true; }
    
private:
    ParallelExecuteLambdaType m_executeLambda;
};

}} // namespace MonsterRender::RDG
