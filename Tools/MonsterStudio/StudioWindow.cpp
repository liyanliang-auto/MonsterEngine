#include "StudioWindow.h"
#include "resource.h"
#include "VideoTo3D/ProjectPreflight.h"

#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

using MonsterEngine::VideoTo3D::ProjectSnapshot;
using MonsterEngine::VideoTo3D::ReconstructionQueue;
using MonsterEngine::VideoTo3D::RunState;
using MonsterEngine::VideoTo3D::ProjectReconstruction;
using MonsterEngine::VideoTo3D::ExecutionState;
using MonsterEngine::VideoTo3D::ExecutionSnapshot;
using MonsterEngine::VideoTo3D::ReconstructionProfile;

namespace {
constexpr UINT kWorkComplete = WM_APP + 1;
constexpr UINT kWorkerStopped = WM_APP + 2;
constexpr UINT kPreviewChanged = WM_APP + 3;

std::wstring String(HINSTANCE instance, UINT id) {
    std::array<wchar_t, 2048> buffer{};
    const int length = LoadStringW(instance, id, buffer.data(), static_cast<int>(buffer.size()));
    return std::wstring(buffer.data(), static_cast<std::size_t>(std::max(length, 0)));
}

std::wstring ToWide(const std::string& source) {
    if (source.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                                           static_cast<int>(source.size()), nullptr, 0);
    if (length == 0) throw std::runtime_error("UTF-8 conversion failed");
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.data(),
                             static_cast<int>(source.size()), result.data(), length)) {
        throw std::runtime_error("UTF-8 conversion failed");
    }
    return result;
}

std::string ToUtf8(const std::wstring& source) {
    if (source.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                                           static_cast<int>(source.size()), nullptr, 0, nullptr, nullptr);
    if (length == 0) throw std::runtime_error("Unicode conversion failed");
    std::string result(static_cast<std::size_t>(length), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source.data(),
                             static_cast<int>(source.size()), result.data(), length, nullptr, nullptr)) {
        throw std::runtime_error("Unicode conversion failed");
    }
    return result;
}

std::wstring EditText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(control, result.data(), length + 1);
    result.resize(static_cast<std::size_t>(copied));
    return result;
}

UINT StateLabel(RunState state) {
    switch (state) {
    case RunState::Pending: return IDS_STATE_PENDING;
    case RunState::Running: return IDS_STATE_RUNNING;
    case RunState::Pausing: return IDS_STATE_PAUSING;
    case RunState::Canceling: return IDS_STATE_CANCELING;
    case RunState::Interrupted: return IDS_STATE_INTERRUPTED;
    case RunState::Canceled: return IDS_STATE_CANCELED;
    case RunState::Failed: return IDS_STATE_FAILED;
    }
    return IDS_STATE_FAILED;
}

INT_PTR CALLBACK NameDialog(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SetFocus(GetDlgItem(dialog, IDC_CREATE_NAME));
        SendDlgItemMessageW(dialog, IDC_CREATE_NAME, EM_LIMITTEXT, 1024, 0);
        return FALSE;
    }
    if (message == WM_COMMAND) {
        if (LOWORD(wparam) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (LOWORD(wparam) == IDOK) {
            auto* name = reinterpret_cast<std::wstring*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (!name) return TRUE;
            *name = EditText(GetDlgItem(dialog, IDC_CREATE_NAME));
            if (name->empty()) {
                const auto text = String(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE)), IDS_NAME_REQUIRED);
                const auto title = String(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE)), IDS_APP_TITLE);
                MessageBoxW(dialog, text.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
    }
    return FALSE;
}
}

StudioWindow::StudioWindow(HINSTANCE instance) : instance_(instance) {
    const DWORD size = GetEnvironmentVariableW(L"MONSTER_VIDEO_TO3D_CONFIG", nullptr, 0);
    if (size) {
        std::wstring value(size, L'\0');
        const DWORD copied = GetEnvironmentVariableW(L"MONSTER_VIDEO_TO3D_CONFIG", value.data(), size);
        if (copied && copied < size) { value.resize(copied); configPath_ = value; }
    }
}

StudioWindow::~StudioWindow() {
    if (preview_) { preview_->RequestStop(); preview_.reset(); }
    if (auto service = Service()) service->RequestForceStop();
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        commands_.clear();
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();
    if (font_) DeleteObject(font_);
}

bool StudioWindow::Create() {
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = WindowProc;
    type.hInstance = instance_;
    type.lpszClassName = L"MonsterStudioWindow";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    const auto title = String(instance_, IDS_APP_TITLE);
    window_ = CreateWindowExW(0, type.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1320, 800, nullptr, nullptr, instance_, this);
    return window_ != nullptr;
}

LRESULT CALLBACK StudioWindow::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->window_ = window;
    }
    return self ? self->HandleMessage(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

void StudioWindow::CreateControls() {
    const auto create = [this](DWORD style, LPCWSTR kind, UINT textId, int id, DWORD extra = 0) {
        const auto label = textId ? String(instance_, textId) : std::wstring{};
        return CreateWindowExW(extra, kind, label.c_str(), WS_CHILD | WS_VISIBLE | style,
                               0, 0, 10, 10, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    create_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_CREATE, IDC_CREATE);
    open_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_OPEN, IDC_OPEN);
    import_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_IMPORT_VIDEO, IDC_IMPORT_VIDEO);
    inputInfo_ = create(WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                         L"EDIT", 0, IDC_INPUT_INFO, WS_EX_CLIENTEDGE);
    config_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_CONFIG, IDC_CONFIG);
    qualityLabel_ = create(SS_LEFT, L"STATIC", IDS_QUALITY, IDC_QUALITY_LABEL);
    quality_ = create(WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, L"COMBOBOX", 0, IDC_QUALITY);
    runtimeSetup_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_RUNTIME_SETUP, IDC_RUNTIME_SETUP);
    for (UINT id : {IDS_QUALITY_LOW, IDS_QUALITY_MEDIUM, IDS_QUALITY_HIGH}) {
        const auto label = String(instance_, id);
        if (SendMessageW(quality_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())) < 0)
            throw std::runtime_error("Cannot populate reconstruction quality selector");
    }
    ResetProfile();
    start_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_START_RECONSTRUCTION, IDC_START_RECONSTRUCTION);
    pause_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_PAUSE_RECONSTRUCTION, IDC_PAUSE_RECONSTRUCTION);
    resume_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_RESUME_RECONSTRUCTION, IDC_RESUME_RECONSTRUCTION);
    forceStop_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_FORCE_STOP, IDC_FORCE_STOP);
    executionInfo_ = create(WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                            L"EDIT", IDS_EXECUTION_NONE, IDC_EXECUTION_INFO, WS_EX_CLIENTEDGE);
    projectInfo_ = create(SS_LEFT, L"STATIC", IDS_PROJECT_NONE, IDC_PROJECT_INFO);
    requestLabel_ = create(SS_LEFT, L"STATIC", IDS_REQUEST_LABEL, IDC_REQUEST_LABEL);
    request_ = create(WS_TABSTOP | WS_BORDER | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                      L"EDIT", 0, IDC_REQUEST, WS_EX_CLIENTEDGE);
    add_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_ADD, IDC_ADD);
    queueLabel_ = create(SS_LEFT, L"STATIC", IDS_QUEUE_LABEL, IDC_QUEUE_LABEL);
    queueList_ = create(WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                        L"LISTBOX", 0, IDC_QUEUE, WS_EX_CLIENTEDGE);
    up_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_UP, IDC_UP);
    down_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_DOWN, IDC_DOWN);
    cancel_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_CANCEL, IDC_CANCEL);
    unavailable_ = create(SS_LEFT, L"STATIC", IDS_UNAVAILABLE, IDC_UNAVAILABLE);
    status_ = create(WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                      L"EDIT", IDS_STATUS_READY, IDC_STATUS);
    previewLoad_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_PREVIEW_LOAD, IDC_PREVIEW_LOAD);
    previewReset_ = create(WS_TABSTOP | BS_PUSHBUTTON, L"BUTTON", IDS_PREVIEW_RESET, IDC_PREVIEW_RESET);
    previewInfo_ = create(SS_LEFT, L"STATIC", IDS_PREVIEW_UNLOADED, IDC_PREVIEW_INFO);
    WNDCLASSEXW surface{sizeof(surface)};
    surface.lpfnWndProc = PreviewProc; surface.hInstance = instance_;
    surface.lpszClassName = L"MonsterStudioPreviewSurface";
    surface.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    surface.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    if (!RegisterClassExW(&surface) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Cannot register embedded preview surface");
    previewChild_ = CreateWindowExW(0, surface.lpszClassName, L"三维结果预览（拖动旋转，滚轮缩放）",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0, 1, 1, window_, reinterpret_cast<HMENU>(IDC_PREVIEW_SURFACE), instance_, this);
    if (!previewChild_) throw std::runtime_error("Cannot create embedded preview surface");
    preview_ = std::make_unique<StudioPreviewHost>(previewChild_, window_, kPreviewChanged);
    const int height = -MulDiv(10, static_cast<int>(GetDpiForWindow(window_)), 72);
    font_ = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    for (HWND control : {create_, open_, import_, config_, start_, pause_, resume_, forceStop_, executionInfo_, inputInfo_, projectInfo_, requestLabel_, request_, add_, queueLabel_,
                         queueList_, up_, down_, cancel_, unavailable_, status_, previewLoad_, previewReset_, previewInfo_, qualityLabel_, quality_, runtimeSetup_}) {
        if (!control) throw std::runtime_error("Windows control creation failed");
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    SendMessageW(request_, EM_LIMITTEXT, 65535, 0);
    SetWindowTextW(request_, String(instance_, IDS_INITIAL_REQUEST).c_str());
    RefreshInput();
    RefreshExecution();
    SetBusy(false);
}

void StudioWindow::Layout() {
    if (!create_) return;
    RECT bounds{};
    GetClientRect(window_, &bounds);
    const int scale = static_cast<int>(GetDpiForWindow(window_));
    const auto d = [scale](int value) { return MulDiv(value, scale, 96); };
    const int margin = d(12), gap = d(7), buttonWidth = d(145), buttonHeight = d(29);
    const int totalWidth = bounds.right - bounds.left - 2 * margin;
    const int width = std::min(d(600), totalWidth / 2);
    const int rightX = margin + width + gap, rightWidth = std::max(1, totalWidth - width - gap);
    MoveWindow(previewLoad_, rightX, margin, d(180), buttonHeight, TRUE);
    MoveWindow(previewReset_, rightX + d(187), margin, d(110), buttonHeight, TRUE);
    MoveWindow(previewInfo_, rightX, margin + buttonHeight + gap, rightWidth, d(68), TRUE);
    const int surfaceY = margin + buttonHeight + gap + d(72);
    const int surfaceHeight = std::max(1L, bounds.bottom - margin - surfaceY);
    MoveWindow(previewChild_, rightX, surfaceY, rightWidth, surfaceHeight, TRUE);
    if (preview_) preview_->Resize(rightWidth, surfaceHeight, !IsIconic(window_) && bounds.bottom > 0);
    int y = margin;
    MoveWindow(create_, margin, y, buttonWidth, buttonHeight, TRUE);
    MoveWindow(open_, margin + buttonWidth + gap, y, buttonWidth, buttonHeight, TRUE);
    MoveWindow(import_, margin + 2 * (buttonWidth + gap), y, buttonWidth, buttonHeight, TRUE);
    y += buttonHeight + gap;
    MoveWindow(projectInfo_, margin, y, width, d(45), TRUE);
    y += d(49);
    MoveWindow(inputInfo_, margin, y, width, d(64), TRUE);
    y += d(64) + gap;
    const int controlGap = d(7);
    MoveWindow(qualityLabel_, margin, y + d(4), d(70), d(22), TRUE);
    MoveWindow(quality_, margin + d(74), y, d(100), d(180), TRUE);
    MoveWindow(config_, margin + d(181), y, d(128), buttonHeight, TRUE);
    MoveWindow(runtimeSetup_, margin + d(316), y, std::max(d(120), width - d(316)), buttonHeight, TRUE);
    y += buttonHeight + gap;
    const int controlWidth = (width - 3 * controlGap) / 4;
    int column = 0;
    for (HWND control : {start_, pause_, resume_, forceStop_})
        MoveWindow(control, margin + column++ * (controlWidth + controlGap), y, controlWidth, buttonHeight, TRUE);
    y += buttonHeight + gap;
    MoveWindow(executionInfo_, margin, y, width, d(78), TRUE);
    y += d(78) + gap;
    MoveWindow(requestLabel_, margin, y, width, d(20), TRUE);
    y += d(20);
    MoveWindow(request_, margin, y, width, d(42), TRUE);
    y += d(42) + gap;
    MoveWindow(add_, margin, y, d(170), buttonHeight, TRUE);
    y += buttonHeight + gap;
    MoveWindow(queueLabel_, margin, y, width, d(20), TRUE);
    y += d(20);
    const int bottom = bounds.bottom - margin - d(80) - buttonHeight - gap;
    MoveWindow(queueList_, margin, y, width, std::max(d(48), bottom - y), TRUE);
    y = std::max(y + d(48), bottom) + gap;
    MoveWindow(up_, margin, y, d(90), buttonHeight, TRUE);
    MoveWindow(down_, margin + d(99), y, d(90), buttonHeight, TRUE);
    MoveWindow(cancel_, margin + d(198), y, d(110), buttonHeight, TRUE);
    MoveWindow(unavailable_, margin, bounds.bottom - margin - d(74), width, d(28), TRUE);
    MoveWindow(status_, margin, bounds.bottom - margin - d(44), width, d(44), TRUE);
}

void StudioWindow::SetStatus(UINT id, const std::wstring& detail) {
    std::wstring message = String(instance_, id);
    message += detail;
    SetWindowTextW(status_, message.c_str());
}

void StudioWindow::SetBusy(bool busy) {
    busy_ = busy;
    const bool project = !snapshot_.projectId.empty();
    EnableWindow(create_, !busy && !closing_);
    EnableWindow(open_, !busy && !closing_);
    EnableWindow(import_, !busy && !closing_ && project);
    EnableWindow(request_, !busy && !closing_ && project);
    EnableWindow(add_, !busy && !closing_ && project);
    EnableWindow(queueList_, !busy && !closing_ && project);
    EnableWindow(up_, !busy && !closing_ && project);
    EnableWindow(down_, !busy && !closing_ && project);
    EnableWindow(cancel_, !busy && !closing_ && project);
    EnableWindow(config_, !busy && !closing_);
    EnableWindow(quality_, !busy && !closing_);
    EnableWindow(runtimeSetup_, !busy && !closing_);
    EnableWindow(start_, !busy && !closing_ && project && input_.has_value() && inputError_.empty());
    EnableWindow(resume_, !busy && !closing_ && project && execution_.resumeAvailable);
    EnableWindow(pause_, execution_.active && execution_.pauseAvailable && !execution_.controlPending && !closing_);
    EnableWindow(forceStop_, execution_.active && !forceRequested_);
    EnableWindow(previewLoad_, !busy && !closing_ && !pendingRetire_ && project && execution_.state == ExecutionState::Completed);
    EnableWindow(previewReset_, !closing_ && !pendingRetire_ && previewIdentity_.requestId != 0);
    if (busy) SetStatus(IDS_STATUS_BUSY);
}

void StudioWindow::RefreshInput() {
    std::wstring text;
    if (!inputError_.empty()) text = String(instance_, IDS_INPUT_ERROR) + ToWide(inputError_);
    else if (!input_) text = String(instance_, IDS_INPUT_NONE);
    else {
        const auto& input = *input_;
        text = String(instance_, input.kind == MonsterEngine::VideoTo3D::InputKind::Video ? IDS_INPUT_VIDEO : IDS_INPUT_PHOTOS);
        text += L"  ID: " + ToWide(input.inputId) + L"  revision: " + std::to_wstring(input.revision);
        for (const auto& file : input.files) {
            text += L"\r\n" + (directory_ / std::filesystem::path(ToWide(file.relativePath))).wstring();
            text += L"\r\n" + std::to_wstring(file.byteCount) + L" bytes  SHA-256: " + ToWide(file.sha256);
        }
    }
    SetWindowTextW(inputInfo_, text.c_str());
}

void StudioWindow::Refresh() {
    if (snapshot_.projectId.empty()) {
        SetWindowTextW(projectInfo_, String(instance_, IDS_PROJECT_NONE).c_str());
    } else {
        const auto name = ToWide(snapshot_.name);
        const auto id = ToWide(snapshot_.projectId);
        const auto path = directory_.wstring();
        std::array<wchar_t, 8192> info{};
        swprintf_s(info.data(), info.size(), String(instance_, IDS_PROJECT_INFO).c_str(),
                   name.c_str(), id.c_str(), static_cast<unsigned long long>(snapshot_.revision), path.c_str());
        SetWindowTextW(projectInfo_, info.data());
    }
    SendMessageW(queueList_, LB_RESETCONTENT, 0, 0);
    int selectedIndex = -1;
    for (std::size_t i = 0; i < snapshot_.runs.size(); ++i) {
        const auto& run = snapshot_.runs[i];
        std::wstring request = ToWide(run.requestBytes);
        std::replace(request.begin(), request.end(), L'\r', L' ');
        std::replace(request.begin(), request.end(), L'\n', L' ');
        if (request.size() > 110) request = request.substr(0, 110) + L"...";
        std::wstring line = std::to_wstring(i + 1) + L"  [" + String(instance_, StateLabel(run.state)) +
                            L"]  " + ToWide(run.runId) + L"  " + request;
        SendMessageW(queueList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        if (run.runId == selectedRunId_) selectedIndex = static_cast<int>(i);
    }
    if (selectedIndex >= 0) SendMessageW(queueList_, LB_SETCURSEL, selectedIndex, 0);
    RefreshInput();
    SetBusy(false);
}

void StudioWindow::StartWorker() { worker_ = std::thread([this] { WorkerLoop(); }); }

void StudioWindow::ReportStage(const Command& command, UINT stage) {
    Completion update{};
    update.operation = command.operation;
    update.generation = command.generation;
    update.final = false;
    update.stage = stage;
    {
        std::lock_guard lock(mutex_);
        if (!stopping_) completions_.push_back(std::move(update));
    }
    PostMessageW(window_, kWorkComplete, 0, 0);
#ifdef MONSTER_STUDIO_TESTING
    if (workerStageHook_) workerStageHook_(stage);
#endif
}

void StudioWindow::WorkerLoop() {
    std::unique_ptr<ReconstructionQueue> queue;
    std::shared_ptr<ProjectReconstruction> service;
    for (;;) {
        Command command{};
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !commands_.empty(); });
            if (stopping_) break;
            command = std::move(commands_.front());
            commands_.pop_front();
        }
        Completion completion{};
        completion.operation = command.operation;
        completion.generation = command.generation;
        completion.profile = command.profile;
        try {
            switch (command.operation) {
            case Operation::ConfigureRuntime:
                completion.runtimePath = MonsterEngine::VideoTo3D::ProvisionRuntimeConfiguration(command.directory);
                break;
            case Operation::Create: {
                auto opened = ReconstructionQueue::Create(command.directory, command.text);
                { std::lock_guard lock(mutex_); service_.reset(); }
                service.reset();
                queue = std::move(opened);
                completion.changed = true;
                break;
            }
            case Operation::Open: {
                std::error_code pathError;
                const bool sameProject = queue &&
                    std::filesystem::equivalent(queue->Directory(), command.directory, pathError) && !pathError;
                if (!sameProject) {
                    auto opened = ReconstructionQueue::Open(command.directory);
                    { std::lock_guard lock(mutex_); service_.reset(); }
                    service.reset();
                    queue = std::move(opened);
                }
                completion.changed = true;
                break;
            }
            case Operation::Add:
                if (!queue) throw std::runtime_error("No project is open");
                completion.selectedRunId = queue->AddPlannedRun(command.text);
                completion.changed = true;
                break;
            case Operation::MoveUp:
            case Operation::MoveDown:
                if (!queue) throw std::runtime_error("No project is open");
                completion.changed = queue->MoveWaiting(command.text, command.operation == Operation::MoveUp ? -1 : 1);
                completion.selectedRunId = command.text;
                break;
            case Operation::Cancel:
                if (!queue) throw std::runtime_error("No project is open");
                completion.changed = queue->CancelWaiting(command.text);
                completion.selectedRunId = command.text;
                break;
            case Operation::InspectPreview:
                if (!queue) throw std::runtime_error("No project is open");
                if (!service) service = std::make_shared<ProjectReconstruction>(*queue, std::filesystem::path{});
                completion.execution = service->Inspect();
                if (completion.execution->state != ExecutionState::Completed || completion.execution->output.empty())
                    throw std::runtime_error("当前项目没有通过校验的已完成结果，请完成重建后重试。");
                break;
            case Operation::StartReconstruction:
            case Operation::ResumeReconstruction: {
                if (!queue) throw std::runtime_error("No project is open");
                service = std::make_shared<ProjectReconstruction>(*queue, command.directory, command.profile);
                { std::lock_guard lock(mutex_); service_ = service; }
                const auto observer = [this, &command](const ExecutionSnapshot& observed) {
                    Completion update{};
                    update.operation = command.operation; update.generation = command.generation;
                    update.final = false; update.execution = observed;
                    { std::lock_guard lock(mutex_); completions_.push_back(std::move(update)); }
                    PostMessageW(window_, kWorkComplete, 0, 0);
                };
                completion.execution = command.operation == Operation::StartReconstruction
                    ? service->RunNew(observer) : service->Resume(observer);
                if (completion.execution->state == ExecutionState::Completed)
                    completion.execution = service->Inspect();
                break;
            }
            case Operation::ImportVideo: {
                using namespace MonsterEngine::VideoTo3D;
                if (!queue) throw std::runtime_error("No project is open");
                ReportStage(command, IDS_IMPORT_PREFLIGHT);
                std::error_code sourceError;
                if (!std::filesystem::is_regular_file(command.directory, sourceError) || sourceError)
                    throw std::runtime_error("请选择仍然存在的普通视频文件；检查路径和读取权限。");
                const auto bytes = std::filesystem::file_size(command.directory, sourceError);
                if (sourceError) throw std::runtime_error("无法读取视频大小，请检查文件和权限。");
                if (!bytes || bytes > kMaxInputFileBytes)
                    throw std::runtime_error("视频为空或超出单文件 16 GiB 限制。");
                SpaceBudget budget;
                budget.mediaCopies = bytes;
                budget.preparedImages = 0; budget.reconstructionDatabase = 0;
                budget.checkpoints = 0; budget.temporaryExports = 0; budget.lod = 0;
                budget.newAssets = kMaxInputManifestBytes;
                budget.retainedOverlap = 0; budget.reserve = 0;
                const auto space = QuerySpacePreflight(queue->Directory(), budget);
                if (space.diagnostic.code != PreflightCode::Ok)
                    throw std::runtime_error("导入空间预检失败；请检查可用空间和项目目录：" + space.diagnostic.message);
                const auto write = ProbeDirectoryWrite(queue->Directory());
                if (write.diagnostic.code != PreflightCode::Ok)
                    throw std::runtime_error("项目写入预检失败；请检查权限和磁盘：" + write.diagnostic.message);
                ReportStage(command, IDS_IMPORT_COPYING);
                completion.input = queue->ImportVideo(command.directory);
                completion.inputInspected = true;
                break;
            }
            }
            if (queue && (command.operation == Operation::Open || command.operation == Operation::Create)) {
                ReportStage(command, IDS_INPUT_INSPECTING);
                completion.inputInspected = true;
                service = std::make_shared<ProjectReconstruction>(*queue, std::filesystem::path{});
                { std::lock_guard lock(mutex_); service_ = service; }
                try { completion.execution = service->Inspect(); }
                catch (const std::exception& error) {
                    ExecutionSnapshot failure; failure.state = ExecutionState::Failed;
                    failure.message = std::string("Cannot inspect reconstruction metadata: ") + error.what();
                    completion.execution = std::move(failure);
                }
                try { completion.input = queue->InspectInput(); }
                catch (const MonsterEngine::VideoTo3D::InputError& error) {
                    if (error.Code() != MonsterEngine::VideoTo3D::InputErrorCode::NoInput)
                        completion.inputError = error.what();
                }
                catch (const std::exception& error) { completion.inputError = error.what(); }
                catch (...) { completion.inputError = "输入校验遇到未知错误；项目仍可打开，请检查已有输入。"; }
            }
            if (queue) {
                completion.snapshot = queue->Snapshot();
                completion.directory = queue->Directory();
            }
        } catch (const std::exception& error) {
            completion.error = command.operation == Operation::ImportVideo
                ? std::string("视频导入失败，请检查视频格式、源文件及项目权限；原有输入和待办计划保留。详细信息：") + error.what()
                : error.what();
        } catch (...) {
            completion.error = "操作遇到未知错误，请检查项目与源文件后重试。";
        }
        {
            std::lock_guard lock(mutex_);
            if (!stopping_) completions_.push_back(std::move(completion));
        }
        PostMessageW(window_, kWorkComplete, 0, 0);
    }
    { std::lock_guard lock(mutex_); service_.reset(); }
    service.reset();
    queue.reset();
    {
        std::lock_guard lock(mutex_);
        workerDone_ = true;
    }
    PostMessageW(window_, kWorkerStopped, 0, 0);
}

void StudioWindow::Enqueue(Command command) {
    if (busy_ || closing_) return;
    const bool needsRetire = command.operation == Operation::Open || command.operation == Operation::Create ||
        command.operation == Operation::ImportVideo || command.operation == Operation::StartReconstruction ||
        command.operation == Operation::ResumeReconstruction || command.operation == Operation::InspectPreview;
    if (preview_ && needsRetire) {
        previewIdentity_ = {};
        pendingRetire_ = ++retireToken_;
        afterRetire_ = std::move(command);
        preview_->RequestRetire(pendingRetire_);
        PreviewText(L"正在释放上次预览资源，请稍候…");
        SetBusy(true);
        return;
    }
    Dispatch(std::move(command));
}

void StudioWindow::Dispatch(Command command) {
    {
        std::lock_guard lock(mutex_);
        commands_.push_back(std::move(command));
    }
    SetBusy(true);
    wake_.notify_one();
}

void StudioWindow::PreviewText(const std::wstring& text) {
    SetWindowTextW(previewInfo_, text.c_str());
}

void StudioWindow::LoadPreview(const Completion& completion) {
    if (closing_ || pendingRetire_ || !preview_ || !completion.execution) return;
    const auto& result = *completion.execution;
    if (result.state != ExecutionState::Completed || result.output.empty()) {
        PreviewText(L"尚无可查看的已完成结果。完成重建后将自动显示。");
        return;
    }
    StudioPreviewRequest request;
    request.identity.projectDirectory = directory_;
    request.identity.projectGeneration = generation_;
    request.identity.executionId = result.executionId;
    request.identity.requestId = ++previewRequest_;
    request.identity.resultGeneration = result.resultGeneration;
    request.identity.contentHash = result.outputSha256;
    request.output = result.output;
    request.initialView = result.initialView;
    previewIdentity_ = request.identity;
    PreviewText(L"正在加载项目已验证的三维结果…界面可继续操作。");
    preview_->RequestLoad(std::move(request));
    SetBusy(busy_);
}

void StudioWindow::PollPreview() {
    if (!preview_) return;
    const auto status = preview_->Snapshot();
    if (pendingRetire_ && status.retiredToken >= pendingRetire_) {
        pendingRetire_ = 0;
        if (afterRetire_ && !closing_) {
            auto command = std::move(*afterRetire_); afterRetire_.reset();
            if (command.operation == Operation::StartReconstruction || command.operation == Operation::ResumeReconstruction)
                PreviewText(L"重建期间暂停预览。真实运行状态见左侧，完成后自动显示新结果。");
            else PreviewText(L"正在校验当前项目结果…");
            Dispatch(std::move(command));
        } else afterRetire_.reset();
    }
    if (status.sequence == previewSequence_) return;
    previewSequence_ = status.sequence;
    if (!previewIdentity_.requestId || status.identity.requestId != previewIdentity_.requestId ||
        status.identity.projectGeneration != previewIdentity_.projectGeneration ||
        status.identity.projectDirectory != directory_ || status.identity.executionId != execution_.executionId) return;
    switch (status.state) {
    case StudioPreviewState::Loading: PreviewText(L"正在加载三维结果…可继续操作界面。"); break;
    case StudioPreviewState::Ready:
        PreviewText(L"三维结果已显示（" + std::to_wstring(status.gaussianCount) + L" 高斯）。拖动旋转，滚轮缩放；可复位视角。"); break;
    case StudioPreviewState::Failed:
        PreviewText(L"预览失败：" + ToWide(status.error) + L"。请检查结果/显卡后点击“查看结果 / 重新加载”。"); break;
    default: break;
    }
}

void StudioWindow::OnCompletion() {
    std::deque<Completion> ready;
    {
        std::lock_guard lock(mutex_);
        ready.swap(completions_);
    }
    for (auto& completion : ready) {
        if (completion.generation != generation_) continue;
        if (completion.execution) {
            const auto& observed = *completion.execution;
            if (!execution_.executionId.empty() && !observed.executionId.empty() &&
                execution_.executionId != observed.executionId) continue;
            execution_ = observed;
            RefreshExecution();
            SetBusy(busy_);
        }
        if (!completion.final) {
            if (!completion.execution) SetStatus(completion.stage);
            ContinueClose();
            continue;
        }
        if (!completion.error.empty()) {
            SetBusy(false);
            const auto detail = ToWide(completion.error);
            SetStatus(IDS_STATUS_ERROR, detail);
            if (completion.operation == Operation::InspectPreview)
                PreviewText(L"结果校验失败：" + detail + L"。请检查项目后重试。");
            if (completion.operation == Operation::StartReconstruction)
                SetStatus(IDS_STATUS_ERROR, detail + L"。请检查运行环境；需要安装或修复时，点击“设置运行环境…”。");
            ContinueClose();
            continue;
        }
        if (completion.operation == Operation::ConfigureRuntime) {
            if (completion.profile == ReconstructionProfile::Advanced) {
                advancedPath_ = std::move(completion.runtimePath);
                profile_ = ReconstructionProfile::Advanced;
                SendMessageW(quality_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
                SetWindowTextW(qualityLabel_, String(instance_, IDS_QUALITY_ADVANCED).c_str());
            }
            RefreshExecution(); SetBusy(false);
            SetStatus(IDS_RUNTIME_READY);
            ContinueClose();
            continue;
        }
        if (completion.operation == Operation::Create || completion.operation == Operation::Open) {
            ResetProfile(); RefreshExecution();
        }
        snapshot_ = std::move(completion.snapshot);
        directory_ = std::move(completion.directory);
        if (completion.inputInspected) {
            input_ = std::move(completion.input);
            inputError_ = std::move(completion.inputError);
        }
        if (!completion.selectedRunId.empty()) selectedRunId_ = std::move(completion.selectedRunId);
        else if (completion.changed) selectedRunId_.clear();
        Refresh();
        SetStatus(completion.changed ? IDS_STATUS_SAVED : IDS_STATUS_NO_CHANGE);
        if (completion.operation == Operation::ImportVideo) {
            execution_ = {}; RefreshExecution(); SetBusy(false); SetStatus(IDS_IMPORT_SUCCESS);
        }
        if (completion.operation == Operation::StartReconstruction || completion.operation == Operation::ResumeReconstruction) {
            forceRequested_ = false;
            RefreshExecution();
            SetStatus(execution_.state == ExecutionState::Failed ? IDS_STATUS_ERROR : IDS_STATUS_READY,
                      L" " + ToWide(execution_.message));
            if (completion.operation == Operation::StartReconstruction && execution_.state == ExecutionState::Failed)
                SetStatus(IDS_STATUS_ERROR, ToWide(execution_.message) + L"。请检查运行环境；需要安装或修复时，点击“设置运行环境…”。");
        }
        if (!inputError_.empty()) SetStatus(IDS_INPUT_ERROR, ToWide(inputError_));
        if (completion.operation == Operation::Open && completion.changed && !snapshot_.runs.empty()) {
            const bool interrupted = std::any_of(snapshot_.runs.begin(), snapshot_.runs.end(),
                [](const auto& run) { return run.state == RunState::Interrupted; });
            if (interrupted) SetStatus(IDS_INTERRUPTED_NOTE);
        }
        if (completion.operation == Operation::Open || completion.operation == Operation::Create ||
            completion.operation == Operation::InspectPreview || completion.operation == Operation::StartReconstruction ||
            completion.operation == Operation::ResumeReconstruction) LoadPreview(completion);
        if (completion.operation == Operation::ImportVideo)
            PreviewText(L"输入已更新。请开始重建，完成后将自动显示新结果。");
        ContinueClose();
    }
}


std::shared_ptr<ProjectReconstruction> StudioWindow::Service() {
    std::lock_guard lock(mutex_);
    return service_;
}

void StudioWindow::RefreshExecution() {
    std::wstring text;
    if (profile_ == ReconstructionProfile::Advanced) text = L"下次新建：高级自定义配置";
    else {
        const auto settings = MonsterEngine::VideoTo3D::GetReconstructionProfileSettings(profile_);
        const UINT label = profile_ == ReconstructionProfile::Low ? IDS_QUALITY_LOW :
            profile_ == ReconstructionProfile::High ? IDS_QUALITY_HIGH : IDS_QUALITY_MEDIUM;
        text = L"下次新建：" + String(instance_, label) + L"，" + std::to_wstring(settings.brushSteps) +
            L" 步 / 最大边 " + std::to_wstring(settings.maxResolution) + L" / 上限 " + std::to_wstring(settings.maxSplats) + L" 高斯";
    }
    text += L"；恢复沿用原任务配置。";
    if (execution_.state == ExecutionState::None) text += L"\r\n" + String(instance_, IDS_EXECUTION_NONE);
    else {
        text += L"\r\n" + ToWide(MonsterEngine::VideoTo3D::ExecutionStateName(execution_.state)) +
                L"  " + ToWide(execution_.executionId) + L"  " + ToWide(execution_.stage);
        text += L"\r\n" + ToWide(execution_.message);
        if (!execution_.profileName.empty()) {
            text += L"\r\n本任务实际：" + ToWide(execution_.profileName) + L" / " +
                std::to_wstring(execution_.effectiveSettings.brushSteps) + L" 步 / 最大边 " +
                std::to_wstring(execution_.effectiveSettings.maxResolution) + L" / 上限 " +
                std::to_wstring(execution_.effectiveSettings.maxSplats) + L" 高斯";
        }
        if (!execution_.directory.empty()) text += L"\r\n运行/日志: " + execution_.directory.wstring();
        if (!execution_.output.empty()) text += L"\r\n已验证输出: " + execution_.output.wstring();
    }
    SetWindowTextW(executionInfo_, text.c_str());
}

void StudioWindow::ResetProfile() {
    profile_ = ReconstructionProfile::Medium;
    advancedPath_.clear();
    SendMessageW(quality_, CB_SETCURSEL, 1, 0);
    SetWindowTextW(qualityLabel_, String(instance_, IDS_QUALITY).c_str());
}

void StudioWindow::StopWorker() {
    { std::lock_guard lock(mutex_); stopping_ = true; commands_.clear(); }
    wake_.notify_one();
}

void StudioWindow::ContinueClose() {
    if (!closing_) return;
    if (afterRetire_) { afterRetire_.reset(); busy_ = false; }
    if (busy_) {
        auto service = Service();
        if (!service) return; // Queued import/start/open must first report its outcome.
        const auto current = service->Snapshot();
        if (!current.active) return; // Keep pumping until the worker's final completion.
        execution_ = current;
        if (current.stage == "control-error" ||
            (closePauseRequested_ && !current.controlPending && current.state == ExecutionState::Training)) {
            closing_ = false; closePauseRequested_ = false; closeForceRequested_ = false;
            RefreshExecution(); SetBusy(true);
            SetStatus(IDS_STATUS_ERROR, L"安全暂停未成功，窗口和控制器已保留。请重试暂停或明确强制停止。 " + ToWide(current.message));
            return;
        }
        if (current.state == ExecutionState::Training || current.state == ExecutionState::Pausing ||
            current.state == ExecutionState::Saving || current.state == ExecutionState::Restoring) {
            if (current.controlPending) closePauseRequested_ = !forceRequested_;
            else if (current.pauseAvailable && !closePauseRequested_) {
                try {
                    if (service->RequestPause()) closePauseRequested_ = true;
                    else throw std::runtime_error("Safe pause request was rejected; window retained");
                } catch (const std::exception& error) {
                    closing_ = false; SetBusy(true); SetStatus(IDS_STATUS_ERROR, ToWide(error.what()));
                }
            }
        } else if (!closeForceRequested_) {
            if (service->RequestForceStop()) { closeForceRequested_ = true; forceRequested_ = true; }
        }
        execution_ = service->Snapshot(); RefreshExecution(); SetBusy(true);
        return;
    }
    // The final completion is delivered only after child/tree retirement. A failed
    // safe save/exit is actionable and must not make the close request destructive.
    if ((closePauseRequested_ || closeForceRequested_) && execution_.state != ExecutionState::Paused && execution_.state != ExecutionState::Completed &&
        execution_.state != ExecutionState::Stopped) {
        closing_ = false; closePauseRequested_ = false; closeForceRequested_ = false;
        SetBusy(false); SetStatus(IDS_STATUS_ERROR, L"未确认安全保存及退出，窗口已保留。 " + ToWide(execution_.message));
        return;
    }
    // Keep the queue lease and borrowed child HWND alive until the owner has
    // retired every GPU resource. Never join a live renderer on the UI thread.
    if (preview_ && !preview_->IsStopped()) {
        previewIdentity_ = {};
        preview_->RequestStop();
        PreviewText(L"正在关闭三维预览并释放 GPU 资源…");
        return;
    }
    if (preview_) preview_.reset();
    StopWorker();
}

bool StudioWindow::SelectConfiguration(std::filesystem::path& path) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) { SetStatus(IDS_STATUS_ERROR, L"无法创建配置文件选择框。"); return false; }
    DWORD options = 0;
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC filters[]{{L"重建配置 (*.json)", L"*.json"}};
    if (SUCCEEDED(result)) result = dialog->SetFileTypes(1, filters);
    const auto title = String(instance_, IDS_SELECT_CONFIG);
    if (SUCCEEDED(result)) result = dialog->SetTitle(title.c_str());
    if (SUCCEEDED(result)) result = dialog->Show(window_);
    const bool canceled = result == HRESULT_FROM_WIN32(ERROR_CANCELLED);
    IShellItem* item = nullptr; PWSTR selected = nullptr;
    if (SUCCEEDED(result)) result = dialog->GetResult(&item);
    if (SUCCEEDED(result)) result = item->GetDisplayName(SIGDN_FILESYSPATH, &selected);
    if (SUCCEEDED(result) && !selected) result = E_FAIL;
    std::wstring copy;
    try { if (SUCCEEDED(result)) copy = selected; }
    catch (...) {
        if (selected) CoTaskMemFree(selected);
        if (item) item->Release();
        dialog->Release(); throw;
    }
    if (selected) CoTaskMemFree(selected);
    if (item) item->Release();
    dialog->Release();
    if (canceled) return false;
    if (FAILED(result)) { SetStatus(IDS_STATUS_ERROR, L"无法读取所选配置文件路径。"); return false; }
    path = std::filesystem::path(copy);
    return true;
}

bool StudioWindow::SelectDirectory(UINT titleId, std::filesystem::path& directory) {
    IFileDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
    if (FAILED(result)) { SetStatus(IDS_FOLDER_ERROR); return false; }
    const auto title = String(instance_, titleId);
    DWORD options = 0;
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(result)) result = dialog->SetTitle(title.c_str());
    if (SUCCEEDED(result)) result = dialog->Show(window_);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { dialog->Release(); return false; }
    IShellItem* item = nullptr;
    PWSTR path = nullptr;
    if (SUCCEEDED(result)) result = dialog->GetResult(&item);
    if (SUCCEEDED(result)) result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (SUCCEEDED(result) && path) directory = std::filesystem::path(path);
    if (path) CoTaskMemFree(path);
    if (item) item->Release();
    dialog->Release();
    if (FAILED(result)) {
        const auto message = String(instance_, IDS_FOLDER_ERROR);
        MessageBoxW(window_, message.c_str(), String(instance_, IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

bool StudioWindow::SelectVideo(std::filesystem::path& source) {
    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
    if (FAILED(result)) { SetStatus(IDS_VIDEO_ERROR); return false; }
    DWORD options = 0;
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result)) result = dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    const COMDLG_FILTERSPEC filters[]{{L"视频 (*.mp4;*.mov;*.mkv;*.avi)", L"*.mp4;*.mov;*.mkv;*.avi"}};
    if (SUCCEEDED(result)) result = dialog->SetFileTypes(1, filters);
    const auto title = String(instance_, IDS_SELECT_VIDEO);
    if (SUCCEEDED(result)) result = dialog->SetTitle(title.c_str());
    if (SUCCEEDED(result)) result = dialog->Show(window_);
    const bool canceled = result == HRESULT_FROM_WIN32(ERROR_CANCELLED);
    IShellItem* item = nullptr;
    PWSTR path = nullptr;
    if (SUCCEEDED(result)) result = dialog->GetResult(&item);
    if (SUCCEEDED(result)) result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (SUCCEEDED(result) && !path) result = E_FAIL;
    // Release COM resources even if allocating the filesystem path throws.
    std::wstring selected;
    try { if (SUCCEEDED(result)) selected = path; }
    catch (...) {
        if (path) CoTaskMemFree(path);
        if (item) item->Release();
        dialog->Release();
        throw;
    }
    if (path) CoTaskMemFree(path);
    if (item) item->Release();
    dialog->Release();
    if (canceled) return false;
    if (FAILED(result)) { SetStatus(IDS_VIDEO_ERROR); return false; }
    source = std::filesystem::path(selected);
    return true;
}

bool StudioWindow::PromptProjectName(std::wstring& name) {
    return DialogBoxParamW(instance_, MAKEINTRESOURCEW(IDD_CREATE_PROJECT), window_, NameDialog,
                           reinterpret_cast<LPARAM>(&name)) == IDOK;
}

std::string StudioWindow::SelectedRunId() const {
    const LRESULT index = SendMessageW(queueList_, LB_GETCURSEL, 0, 0);
    if (index < 0 || static_cast<std::size_t>(index) >= snapshot_.runs.size()) return {};
    return snapshot_.runs[static_cast<std::size_t>(index)].runId;
}

void StudioWindow::OnCommand(WORD id, WORD notification) {
    if (id == IDC_QUALITY && notification == CBN_SELCHANGE) {
        if (busy_ || closing_) return;
        const auto selection = SendMessageW(quality_, CB_GETCURSEL, 0, 0);
        if (selection < 0 || selection > 2) { SetStatus(IDS_STATUS_ERROR, L"请选择低、中或高预设。"); return; }
        profile_ = selection == 0 ? ReconstructionProfile::Low : selection == 2 ? ReconstructionProfile::High : ReconstructionProfile::Medium;
        SetWindowTextW(qualityLabel_, String(instance_, IDS_QUALITY).c_str());
        RefreshExecution();
        return;
    }
    if (id == IDC_QUEUE && notification == LBN_SELCHANGE) {
        selectedRunId_ = SelectedRunId();
        return;
    }
    if (notification != BN_CLICKED) return;
    if (id == IDC_PREVIEW_RESET) {
        if (preview_ && !closing_ && !pendingRetire_) preview_->ResetView();
        return;
    }
    if (id == IDC_PAUSE_RECONSTRUCTION || id == IDC_FORCE_STOP) {
        try {
            auto service = Service();
            if (!service || !busy_) return;
            const bool pause = id == IDC_PAUSE_RECONSTRUCTION;
            if (pause && closing_) return;
            if (!pause && forceRequested_) return;
            const bool accepted = pause ? service->RequestPause() : service->RequestForceStop();
            if (!accepted) { SetStatus(IDS_STATUS_ERROR, L"控制请求已被拒绝：阶段不支持或已有请求待处理。"); return; }
            if (!pause) forceRequested_ = true;
            execution_ = service->Snapshot(); RefreshExecution(); SetBusy(true);
        } catch (const std::exception& error) { SetStatus(IDS_STATUS_ERROR, ToWide(error.what())); }
        return;
    }
    if (busy_ || closing_) return;
    try {
        if (id == IDC_PREVIEW_LOAD) {
            if (execution_.state == ExecutionState::Completed)
                Enqueue({Operation::InspectPreview, ++generation_, {}, {}});
        } else if (id == IDC_CONFIG || id == IDC_RUNTIME_SETUP) {
            std::filesystem::path path;
            if (SelectConfiguration(path))
                Enqueue({Operation::ConfigureRuntime, ++generation_, std::move(path), {},
                    id == IDC_CONFIG ? ReconstructionProfile::Advanced : ReconstructionProfile::Medium});
        } else if (id == IDC_START_RECONSTRUCTION || id == IDC_RESUME_RECONSTRUCTION) {
            if (snapshot_.projectId.empty()) return;
            if (id == IDC_RESUME_RECONSTRUCTION && !execution_.resumeAvailable) return;
            const auto selectedProfile = profile_;
            const auto basePath = selectedProfile == ReconstructionProfile::Advanced ? advancedPath_ : configPath_;
            execution_ = {}; forceRequested_ = false;
            RefreshExecution();
            Enqueue({id == IDC_START_RECONSTRUCTION ? Operation::StartReconstruction : Operation::ResumeReconstruction,
                     ++generation_, basePath, {}, selectedProfile});
        } else if (id == IDC_CREATE) {
            std::filesystem::path parent;
            if (!SelectDirectory(IDS_SELECT_PARENT, parent)) return;
            std::wstring name;
            if (!PromptProjectName(name)) return;
            execution_ = {};
            Enqueue({Operation::Create, ++generation_, parent / name, ToUtf8(name)});
        } else if (id == IDC_OPEN) {
            std::filesystem::path directory;
            if (!SelectDirectory(IDS_SELECT_PROJECT, directory)) return;
            execution_ = {};
            Enqueue({Operation::Open, ++generation_, directory, {}});
        } else if (id == IDC_IMPORT_VIDEO) {
            if (snapshot_.projectId.empty()) return;
            std::filesystem::path source;
            if (!SelectVideo(source)) return;
            Enqueue({Operation::ImportVideo, ++generation_, source, {}});
        } else if (id == IDC_ADD) {
            const std::string request = ToUtf8(EditText(request_));
            if (request.empty()) { SetStatus(IDS_REQUEST_REQUIRED); return; }
            if (request.size() > MonsterEngine::VideoTo3D::kMaxRequestBytes) {
                SetStatus(IDS_REQUEST_TOO_LARGE);
                return;
            }
            Enqueue({Operation::Add, ++generation_, {}, request});
        } else if (id == IDC_UP || id == IDC_DOWN || id == IDC_CANCEL) {
            const auto runId = SelectedRunId();
            if (runId.empty()) { SetStatus(IDS_SELECT_RUN); return; }
            Enqueue({id == IDC_UP ? Operation::MoveUp : id == IDC_DOWN ? Operation::MoveDown : Operation::Cancel,
                     ++generation_, {}, runId});
        }
    } catch (const std::exception& error) {
        SetStatus(IDS_STATUS_ERROR, ToWide(error.what()));
    }
}

LRESULT StudioWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        try {
            CreateControls();
            if (!SetTimer(window_, 1, 100, nullptr)) throw std::runtime_error("Cannot start completion timer");
            StartWorker(); Layout();
        }
        catch (...) { return -1; }
        return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window_, nullptr, rect->left, rect->top, rect->right - rect->left,
                     rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-MulDiv(10, HIWORD(wparam), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
        for (HWND control : {create_, open_, import_, config_, start_, pause_, resume_, forceStop_, executionInfo_, inputInfo_, projectInfo_, requestLabel_, request_, add_, queueLabel_, queueList_,
                             up_, down_, cancel_, unavailable_, status_, previewLoad_, previewReset_, previewInfo_, qualityLabel_, quality_, runtimeSetup_}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        Layout();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        const int dpi = window_ ? static_cast<int>(GetDpiForWindow(window_)) : 96;
        info->ptMinTrackSize = {MulDiv(1180, dpi, 96), MulDiv(760, dpi, 96)};
        return 0;
    }
    case WM_COMMAND: OnCommand(LOWORD(wparam), HIWORD(wparam)); return 0;
    case kWorkComplete: OnCompletion(); return 0;
    case kPreviewChanged: PollPreview(); ContinueClose(); return 0;
    case WM_TIMER: {
        if (wparam != 1) break;
        OnCompletion();
        PollPreview();
        bool finished = false;
        { std::lock_guard lock(mutex_); finished = workerDone_; }
        ContinueClose();
        if (closing_ && finished && (!preview_ || preview_->IsStopped())) {
            preview_.reset(); if (worker_.joinable()) worker_.join(); DestroyWindow(window_);
        }
        return 0;
    }
    case kWorkerStopped:
        ContinueClose();
        return 0;
    case WM_CLOSE: {
        if (!closing_) {
            closing_ = true; closePauseRequested_ = false; closeForceRequested_ = false;
            SetBusy(busy_); SetStatus(IDS_CLOSE_WAIT);
        }
        ContinueClose();
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window_, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

LRESULT CALLBACK StudioWindow::PreviewProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<StudioWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<StudioWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_LBUTTONDOWN:
        if (!self->closing_ && self->previewIdentity_.requestId && !self->pendingRetire_) {
            SetFocus(window); SetCapture(window); self->previewDragging_ = true;
            self->previewMouse_ = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        }
        return 0;
    case WM_LBUTTONUP:
        self->previewDragging_ = false;
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED: self->previewDragging_ = false; return 0;
    case WM_MOUSEMOVE:
        if (self->previewDragging_ && self->preview_ && !self->pendingRetire_ && !self->closing_) {
            RECT bounds{}; GetClientRect(window, &bounds);
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            self->preview_->Orbit(static_cast<float>(point.x - self->previewMouse_.x) / std::max(1L, bounds.right),
                                  static_cast<float>(point.y - self->previewMouse_.y) / std::max(1L, bounds.bottom));
            self->previewMouse_ = point;
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (self->preview_ && self->previewIdentity_.requestId && !self->pendingRetire_ && !self->closing_)
            self->preview_->Zoom(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA);
        return 0;
    case WM_KEYDOWN:
        if (self->preview_ && self->previewIdentity_.requestId && !self->pendingRetire_ && !self->closing_) {
            if (wparam == VK_HOME) self->preview_->ResetView();
            else if (wparam == VK_LEFT || wparam == VK_RIGHT) self->preview_->Orbit(wparam == VK_LEFT ? -.03f : .03f, 0);
            else if (wparam == VK_UP || wparam == VK_DOWN) self->preview_->Orbit(0, wparam == VK_UP ? -.03f : .03f);
            else if (wparam == VK_ADD || wparam == VK_SUBTRACT) self->preview_->Zoom(wparam == VK_ADD ? 1.f : -1.f);
        }
        return 0;
    case WM_ERASEBKGND:
        if (self->preview_ && self->preview_->Snapshot().state == StudioPreviewState::Ready) return 1;
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
