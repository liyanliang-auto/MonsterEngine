#pragma once

#include "VideoTo3D/ReconstructionQueue.h"
#include "VideoTo3D/ProjectReconstruction.h"
#include "StudioPreviewHost.h"

#include <windows.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <optional>
#ifdef MONSTER_STUDIO_TESTING
#include <functional>
#endif

class StudioWindow final {
public:
    explicit StudioWindow(HINSTANCE instance);
    ~StudioWindow();
    bool Create();
    HWND Handle() const noexcept { return window_; }

private:
    enum class Operation { Create, Open, Add, MoveUp, MoveDown, Cancel, ImportVideo, StartReconstruction, ResumeReconstruction, InspectPreview, ConfigureRuntime };
    struct Command {
        Operation operation;
        std::uint64_t generation;
        std::filesystem::path directory;
        std::string text;
        MonsterEngine::VideoTo3D::ReconstructionProfile profile{MonsterEngine::VideoTo3D::ReconstructionProfile::Medium};
    };
    struct Completion {
        Operation operation{Operation::Open};
        std::uint64_t generation;
        bool changed{false};
        std::string selectedRunId;
        std::string error;
        std::filesystem::path directory;
        MonsterEngine::VideoTo3D::ProjectSnapshot snapshot;
        bool final{true};
        UINT stage{0};
        bool inputInspected{false};
        std::optional<MonsterEngine::VideoTo3D::ExecutionSnapshot> execution;
        std::optional<MonsterEngine::VideoTo3D::InputManifest> input;
        std::string inputError;
        std::filesystem::path runtimePath;
        MonsterEngine::VideoTo3D::ReconstructionProfile profile{MonsterEngine::VideoTo3D::ReconstructionProfile::Medium};
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK PreviewProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
    void CreateControls();
    void Layout();
    void Refresh();
    void StartWorker();
    void WorkerLoop();
    void Enqueue(Command command);
    void OnCompletion();
    void OnCommand(WORD id, WORD notification);
    bool SelectDirectory(UINT titleId, std::filesystem::path& directory);
    bool SelectVideo(std::filesystem::path& source);
    void ReportStage(const Command& command, UINT stage);
    void RefreshInput();
    bool SelectConfiguration(std::filesystem::path& path);
    void RefreshExecution();
    void ResetProfile();
    void ContinueClose();
    void StopWorker();
    void Dispatch(Command command);
    void PollPreview();
    void LoadPreview(const Completion& completion);
    void PreviewText(const std::wstring& text);
    std::shared_ptr<MonsterEngine::VideoTo3D::ProjectReconstruction> Service();
    bool PromptProjectName(std::wstring& name);
    std::string SelectedRunId() const;
    void SetStatus(UINT id, const std::wstring& detail = {});
    void SetBusy(bool busy);

    HINSTANCE instance_{};
    HWND window_{};
    HWND create_{};
    HWND open_{};
    HWND import_{};
    HWND inputInfo_{};
    HWND config_{};
    HWND qualityLabel_{}, quality_{}, runtimeSetup_{};
    HWND start_{};
    HWND pause_{};
    HWND resume_{};
    HWND forceStop_{};
    HWND executionInfo_{};
    HWND projectInfo_{};
    HWND requestLabel_{};
    HWND request_{};
    HWND add_{};
    HWND queueLabel_{};
    HWND queueList_{};
    HWND up_{};
    HWND down_{};
    HWND cancel_{};
    HWND unavailable_{};
    HWND status_{};
    HWND previewChild_{}, previewInfo_{}, previewLoad_{}, previewReset_{};
    std::unique_ptr<StudioPreviewHost> preview_;
    StudioPreviewIdentity previewIdentity_;
    std::optional<Command> afterRetire_;
    std::uint64_t previewRequest_{0}, retireToken_{0}, pendingRetire_{0}, previewSequence_{0};
    bool previewDragging_{false};
    POINT previewMouse_{};
    HFONT font_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    std::deque<Completion> completions_;
    std::thread worker_;
    bool stopping_{false};
    bool workerDone_{false};
    bool busy_{false};
    bool closing_{false};
    bool closePauseRequested_{false};
    bool closeForceRequested_{false};
    bool forceRequested_{false};
    std::filesystem::path configPath_;
    std::filesystem::path advancedPath_;
    MonsterEngine::VideoTo3D::ReconstructionProfile profile_{MonsterEngine::VideoTo3D::ReconstructionProfile::Medium};
    MonsterEngine::VideoTo3D::ExecutionSnapshot execution_;
    std::shared_ptr<MonsterEngine::VideoTo3D::ProjectReconstruction> service_;
    std::uint64_t generation_{0};
    std::filesystem::path directory_;
    MonsterEngine::VideoTo3D::ProjectSnapshot snapshot_;
    std::string selectedRunId_;
    std::optional<MonsterEngine::VideoTo3D::InputManifest> input_;
    std::string inputError_;
#ifdef MONSTER_STUDIO_TESTING
    friend struct StudioWindowTestAccess;
    // Set before work starts. Tests may block a worker phase without touching controls.
    std::function<void(UINT)> workerStageHook_;
#endif
};
