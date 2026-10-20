#pragma once

#include "VideoTo3D/ReconstructionQueue.h"

#include <windows.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class StudioWindow final {
public:
    explicit StudioWindow(HINSTANCE instance);
    ~StudioWindow();
    bool Create();
    HWND Handle() const noexcept { return window_; }

private:
    enum class Operation { Create, Open, Add, MoveUp, MoveDown, Cancel };
    struct Command {
        Operation operation;
        std::uint64_t generation;
        std::filesystem::path directory;
        std::string text;
    };
    struct Completion {
        Operation operation{Operation::Open};
        std::uint64_t generation;
        bool changed{false};
        std::string selectedRunId;
        std::string error;
        std::filesystem::path directory;
        MonsterEngine::VideoTo3D::ProjectSnapshot snapshot;
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
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
    bool PromptProjectName(std::wstring& name);
    std::string SelectedRunId() const;
    void SetStatus(UINT id, const std::wstring& detail = {});
    void SetBusy(bool busy);

    HINSTANCE instance_{};
    HWND window_{};
    HWND create_{};
    HWND open_{};
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
    std::uint64_t generation_{0};
    std::filesystem::path directory_;
    MonsterEngine::VideoTo3D::ProjectSnapshot snapshot_;
    std::string selectedRunId_;
};
