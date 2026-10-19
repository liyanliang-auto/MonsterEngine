#pragma once
#include "Renderer/Splat/SplatViewerSession.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include <array>
#include <thread>

namespace SplatVerification {
inline void RunPresentationPolicyTests(Suite& suite,Device& d) {
    suite.Test("PRESENT production policy neutral desktop and unity scale",[]{
        for(auto display:std::vector<std::array<uint32_t,2>>{{0,0},{1920,815},{960,407}}){
            const auto p=FSplatViewerSession::EvaluatePresentationScale(1920,815,1920,815,display[0],display[1]);
            Require(p.displayScale==1&&p.sharpeningStrength==0,"neutral policy changed original presentation");
        }
    });
    suite.Test("PRESENT production policy measured MuMu view and bounded gain",[]{
        const auto p=FSplatViewerSession::EvaluatePresentationScale(1920,815,1920,815,2536,1077);
        Require(std::abs(p.displayScale-std::max(2536.0/1920,1077.0/815))<1e-6,"visible/WSI scale mismatch");
        Require(p.sharpeningStrength>0.14f&&p.sharpeningStrength<0.143f,"measured magnification did not select restrained compensation");
        const auto extreme=FSplatViewerSession::EvaluatePresentationScale(1,1,1,1,16384,16384);
        Require(std::isfinite(extreme.displayScale)&&extreme.sharpeningStrength==0.22f,"gain exceeded cap");
    });
    suite.Test("PRESENT production policy every invalid extent falls back",[]{
        for(unsigned index=0;index<6;++index)for(uint32_t invalid:{0u,16385u,UINT32_MAX}){
            std::array<uint32_t,6> e{64,64,64,64,128,128};e[index]=invalid;
            const auto p=FSplatViewerSession::EvaluatePresentationScale(e[0],e[1],e[2],e[3],e[4],e[5]);
            Require(p.displayScale==1&&p.sharpeningStrength==0,"invalid extent enabled compensation");
        }
    });
    suite.Test("PRESENT production policy suppresses duplicate native upscaling",[]{
        for(auto source:std::vector<std::array<uint32_t,2>>{{32,64},{64,32},{32,32}}){
            const auto p=FSplatViewerSession::EvaluatePresentationScale(source[0],source[1],64,64,128,128);
            Require(p.displayScale==2&&p.sharpeningStrength==0,"native upscaling received duplicate correction");
        }
        Require(FSplatViewerSession::EvaluatePresentationScale(63,63,64,64,128,128).sharpeningStrength>0,
            "one-pixel direct-sample tolerance disagrees with fragment path");
    });
    suite.Test("PRESENT actual owner memory binding cache refresh and shutdown",[&]{
        FFileResourceProvider provider(".");FSplatViewerSession session;FViewerConfig config;
        Require(!session.Snapshot()->vulkanManagedLiveBytes,"unobserved manager bytes fabricated");
        const auto start=std::chrono::steady_clock::now();
        Require(session.Initialize(*d.rhi,provider,config).ok,"shared session initialization");
        const auto first=session.Snapshot();uint64 used=0,unused=0;d.rhi->getMemoryStats(used,unused);
        Require(first->vulkanManagedLiveBytes==used&&first->vulkanPooledUnusedBytes==unused&&
            first->vulkanManagedBackingBytes==used+unused,"session observation does not bind existing memory manager");
        auto allocation=Buffer(d.rhi,4u<<20,EMemoryUsage::Upload);
        session.SetPresentationDisplayExtent(128,128);
        if(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(500))
            Require(session.Snapshot()->vulkanManagedLiveBytes==first->vulkanManagedLiveBytes,"owner sampled again inside one second");
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        session.SetPresentationDisplayExtent(0,0);d.rhi->getMemoryStats(used,unused);
        const auto refreshed=session.Snapshot();
        Require(refreshed->vulkanManagedLiveBytes==used&&refreshed->vulkanPooledUnusedBytes==unused&&
            refreshed->vulkanManagedBackingBytes==used+unused,"owner failed to refresh actual cached manager observations");
        Require(*refreshed->vulkanManagedLiveBytes>=*first->vulkanManagedLiveBytes+(4u<<20),"real allocation absent from observation");
        Require(!refreshed->displayWidth&&!refreshed->displayHeight&&refreshed->presentationSharpeningStrength==0,"retirement policy not cleared");
        session.Shutdown();Require(!session.Snapshot()->vulkanManagedLiveBytes&&!session.Snapshot()->vulkanManagedBackingBytes&&
            !session.Snapshot()->vulkanPooledUnusedBytes,"shutdown retained old-device memory observation");
    });
}
} // namespace SplatVerification
