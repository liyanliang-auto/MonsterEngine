// Copyright Monster Engine. All Rights Reserved.
#include "VideoTo3D/ProjectReconstruction.h"
#include "VideoTo3D/ToolAdmission.h"
#include <charconv>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
using namespace MonsterEngine::VideoTo3D;
namespace {
std::string Utf8(const std::filesystem::path& p) { const auto s=p.u8string(); return {reinterpret_cast<const char*>(s.data()),s.size()}; }
void Print(const ExecutionSnapshot& s) {
    std::cout << "{\"state\":\"" << ExecutionStateName(s.state) << "\",\"executionId\":\"" << EscapeJson(s.executionId)
        << "\",\"stage\":\"" << EscapeJson(s.stage) << "\",\"message\":\"" << EscapeJson(s.message)
        << "\",\"directory\":\"" << EscapeJson(Utf8(s.directory)) << "\",\"output\":\"" << EscapeJson(Utf8(s.output))
        << "\",\"active\":" << (s.active?"true":"false") << ",\"pauseAvailable\":" << (s.pauseAvailable?"true":"false")
        << ",\"resumeAvailable\":" << (s.resumeAvailable?"true":"false") << ",\"controlPending\":" << (s.controlPending?"true":"false") << "}\n" << std::flush;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        std::filesystem::path project,config; std::wstring action; bool pauseNext=false; std::uint64_t pauseIteration=std::numeric_limits<std::uint64_t>::max();
        for (int i=1;i<argc;++i) {
            const std::wstring_view option(argv[i]);
            if (option==L"--help") {
                std::cout << "MonsterVideoTo3DReconstruction --project ABS_DIR --config ABS_JSON --action start|resume|inspect [--pause-at-next-iteration | --pause-at-iteration N]\n";
                return 0;
            }
            if (option==L"--pause-at-next-iteration" && !pauseNext && pauseIteration==std::numeric_limits<std::uint64_t>::max()) { pauseNext=true; continue; }
            if (i+1>=argc) throw std::runtime_error("Option requires a value");
            const std::wstring_view value(argv[++i]);
            if (option==L"--project" && project.empty()) project=value;
            else if (option==L"--config" && config.empty()) config=value;
            else if (option==L"--action" && action.empty()) action=value;
            else if (option==L"--pause-at-iteration" && !pauseNext && pauseIteration==std::numeric_limits<std::uint64_t>::max()) {
                if (value.empty() || value.size()>9 || value.find_first_not_of(L"0123456789")!=std::wstring_view::npos) throw std::runtime_error("Pause iteration must be a bounded nonnegative integer");
                pauseIteration=std::stoull(std::wstring(value)); if (!pauseIteration || pauseIteration>10000000) throw std::runtime_error("Pause iteration exceeds bound");
            } else throw std::runtime_error("Unknown or duplicate option");
        }
        if (!project.is_absolute() || (action!=L"start" && action!=L"resume" && action!=L"inspect") || (action!=L"inspect" && !config.is_absolute())) throw std::runtime_error("Explicit project, configuration and start|resume|inspect action are required");
        if (action==L"inspect" && (pauseNext || pauseIteration!=std::numeric_limits<std::uint64_t>::max())) throw std::runtime_error("Inspect cannot request controls");
        auto queue=ReconstructionQueue::Open(project); ProjectReconstruction service(*queue,config);
        if (action==L"inspect") { Print(service.Inspect()); return 0; }
        bool pauseRequested=false;
        const auto observe=[&](const ExecutionSnapshot& s) {
            Print(s);
            if (pauseRequested || !s.pauseAvailable || s.controlPending) return;
            if (pauseIteration!=std::numeric_limits<std::uint64_t>::max()) pauseRequested=service.RequestPauseAtIteration(pauseIteration);
            else if (pauseNext) pauseRequested=service.RequestPause();
        };
        const auto result=action==L"start"?service.RunNew(observe):service.Resume(observe);
        return result.state==ExecutionState::Completed || result.state==ExecutionState::Paused ? 0 : result.state==ExecutionState::Stopped ? 3 : 1;
    } catch (const std::exception& e) {
        std::cerr << "{\"state\":\"Failed\",\"message\":\"" << EscapeJson(e.what()) << "\"}\n"; return 2;
    }
}
