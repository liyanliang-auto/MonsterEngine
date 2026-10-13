// Copyright Monster Engine. All Rights Reserved.
#pragma once
#include "Renderer/Splat/SplatPass.h"
#include "Renderer/Splat/LOD/SplatLODTypes.h"
#include <vector>

namespace MonsterRender::Splat {
/** Current-frame fusion with a conservative temporal certificate and queued exact
 * validation. Checkpoint kernels certify errors; rendering always uses new fits. */
class FSplatSplitFusionPass {
public:
    static bool Preflight(RHI::IRHIDevice*, uint32_t activeCapacity, uint64_t& bytes);
    bool Initialize(RHI::IRHIDevice*, uint32_t activeCapacity, const FSplatPreprocessOutput&);
    /** Sparse ordered disjoint intervals of 2..4 original sources. Others stay fine. */
    bool SetGroups(const std::vector<FSplatFusionGroup>&);
    void InvalidateCheckpoint();
    /** Only call after the owning pipeline accepted a changed real submit token. */
    void ConfirmSubmitted();
    void DiscardRecording();
    bool Execute(RHI::IRHICommandList*, uint32_t slot, uint32_t sourceCount,
        uint32_t gridX, uint32_t gridY, uint64_t frame, float maxError);
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> GetCounter() const { return m_counter; }
    std::vector<MonsterEngine::TSharedPtr<RHI::IRHIBuffer>> GetOwnedBuffers() const;
    static constexpr uint32_t CounterBytes = 48;
private:
    struct FPass {
        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSetLayout> sets;
        MonsterEngine::TSharedPtr<RHI::IRHIPipelineLayout> layout;
        MonsterEngine::TSharedPtr<RHI::IRHIPipelineState> pipeline;
        MonsterEngine::TSharedPtr<RHI::IRHIDescriptorSet> descriptors[2];
    };
    bool Create(FPass&, const char*, const FSplatPreprocessOutput&);
    struct FPush {uint32_t groups,sourceCount,x,y;float error;uint32_t cacheSlots,queueCapacity,frame;};
    void Dispatch(RHI::IRHICommandList*, FPass&, uint32_t slot, const FPush&, uint32_t groups);
    RHI::IRHIDevice* m_device = nullptr;
    uint32_t m_activeCapacity = 0, m_maxGroups = 0, m_cacheSlots = 0;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_groups[2],m_counter,m_checkpoint;
    MonsterEngine::TSharedPtr<RHI::IRHIBuffer> m_freshQueue,m_exactQueue,m_freshArgs,m_exactArgs;
    FPass m_clear,m_visible,m_cheap,m_args,m_exact,m_fresh;
    std::vector<FSplatFusionGroup> m_cpuGroups;
    bool m_checkpointCleared = false, m_pendingClear = false;
};
}
