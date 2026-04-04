// Copyright Monster Engine. All Rights Reserved.

#include "Renderer/RenderPasses.h"
#include "Core/Log.h"

namespace MonsterEngine {
namespace Renderer {

FRenderPassManager::FRenderPassManager() {
    MR_LOG_INFO("FRenderPassManager initialized");
}

FRenderPassManager::~FRenderPassManager() {
    m_registeredPasses.clear();
    MR_LOG_INFO("FRenderPassManager shutdown");
}

void FRenderPassManager::RegisterPass(const FRenderPassDesc& Desc, TSharedPtr<FRenderPassExecutor> Executor) {
    if (!Executor) {
        MR_LOG_ERROR((FString("FRenderPassManager::RegisterPass - Null executor for pass: ") + Desc.Name).ToStdString());
        return;
    }
    
    // Check if pass already registered
    for (const auto& Entry : m_registeredPasses) {
        if (Entry.Desc.Type == Desc.Type) {
            MR_LOG_WARNING((FString("FRenderPassManager::RegisterPass - Pass already registered: ") + Desc.Name).ToStdString());
            return;
        }
    }
    
    FPassEntry Entry;
    Entry.Desc = Desc;
    Entry.Executor = Executor;
    m_registeredPasses.push_back(Entry);
    
    MR_LOG_INFO((FString("FRenderPassManager::RegisterPass - Registered pass: ") + Desc.Name + 
                FString(" (Parallel: ") + FString(Desc.bParallelExecute ? "Yes" : "No") + FString(")")).ToStdString());
}

void FRenderPassManager::ExecutePass(ERenderPassType Type, const FRenderPassContext& Context) {
    for (const auto& Entry : m_registeredPasses) {
        if (Entry.Desc.Type == Type) {
            MR_LOG_DEBUG((FString("FRenderPassManager::ExecutePass - Executing pass: ") + Entry.Desc.Name).ToStdString());
            Entry.Executor->Execute(Context);
            return;
        }
    }
    
    MR_LOG_WARNING("FRenderPassManager::ExecutePass - Pass not found: " + std::to_string(static_cast<uint32>(Type)));
}

void FRenderPassManager::ExecutePassesParallel(const TArray<ERenderPassType>& Passes, const FRenderPassContext& Context) {
    MR_LOG_INFO("FRenderPassManager::ExecutePassesParallel - Executing " + std::to_string(Passes.size()) + " passes in parallel");
    
    // For now, execute sequentially
    // Parallel execution will be implemented in Phase 3
    for (const auto& PassType : Passes) {
        ExecutePass(PassType, Context);
    }
}

const FRenderPassDesc* FRenderPassManager::GetPassDesc(ERenderPassType Type) const {
    for (const auto& Entry : m_registeredPasses) {
        if (Entry.Desc.Type == Type) {
            return &Entry.Desc;
        }
    }
    return nullptr;
}

bool FRenderPassManager::IsPassRegistered(ERenderPassType Type) const {
    for (const auto& Entry : m_registeredPasses) {
        if (Entry.Desc.Type == Type) {
            return true;
        }
    }
    return false;
}

} // namespace Renderer
} // namespace MonsterEngine
