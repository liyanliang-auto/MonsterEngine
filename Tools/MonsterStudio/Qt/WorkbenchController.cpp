#include "WorkbenchController.h"
#include "WorkbenchWindow.h"
#include "NativePreviewWidget.h"
#include "LocalSettings.h"
#include "VideoTo3D/ProjectPreflight.h"
#include <QDateTime>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace MonsterStudioQt {
namespace {
namespace V = MonsterEngine::VideoTo3D;
namespace fs = std::filesystem;
QString Text(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }
QString PathText(const fs::path& p) { return QString::fromStdWString(p.wstring()); }
fs::path Path(const QString& s) { return fs::path(s.toStdWString()); }
std::string Bytes(const QString& s) { return s.toUtf8().toStdString(); }
WorkbenchNewTask UnusedName(WorkbenchNewTask task) {
    const auto base = task.name;
    for (int n = 2; fs::exists(Path(task.root) / Path(task.name)); ++n) {
        if (n > 10000) throw std::runtime_error("Cannot suggest an unused project name; choose another root");
        task.name = base + QStringLiteral(" (%1)").arg(n);
    }
    return task;
}
QString ActiveTime(const V::ExecutionTiming& t) {
    if (!t.activeMilliseconds) return QStringLiteral("未记录");
    const auto seconds = *t.activeMilliseconds / 1000;
    return QStringLiteral("%1:%2:%3%4").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0')).arg(t.activeTimeComplete ? QString{} : QStringLiteral("（已知时长，部分未记录）"));
}
QDateTime RecordedTime(const std::optional<std::string>& value) {
    if (!value) return {};
    const auto parsed = QDateTime::fromString(Text(*value), Qt::ISODateWithMs);
    // A timestamp with no zone cannot establish a trustworthy wall-clock span.
    return parsed.isValid() && parsed.timeSpec() != Qt::LocalTime ? parsed : QDateTime{};
}
QString Timestamp(const std::optional<std::string>& value) {
    const auto parsed = RecordedTime(value);
    return parsed.isValid() ? parsed.toLocalTime().toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'ttt") : QStringLiteral("未记录");
}
QString CompletedTime(const V::ExecutionTiming& t) { return Timestamp(t.completedUtc); }
QString TimingDetails(const V::ExecutionTiming& t) {
    const auto start = RecordedTime(t.startedUtc), end = RecordedTime(t.completedUtc);
    QString span = QStringLiteral("未记录");
    if (start.isValid() && end.isValid() && start <= end) {
        const auto milliseconds = start.msecsTo(end);
        const auto seconds = milliseconds / 1000;
        span = QStringLiteral("%1:%2:%3.%4").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QChar('0'))
            .arg(seconds % 60, 2, 10, QChar('0')).arg(milliseconds % 1000, 3, 10, QChar('0'));
    }
    const bool preparation = t.startedBoundary == std::optional<std::string>{"preparation_start_v1"};
    const bool execution = t.startedBoundary == std::optional<std::string>{"execution_start_v1"};
    const auto boundary = preparation ? QStringLiteral("准备开始") : execution ? QStringLiteral("执行开始（不含前置准备）") : QStringLiteral("旧记录开始（范围未记录）");
    return QStringLiteral("%1：%2\n完成时间：%3\n记录的开始至完成跨度（含暂停等待）：%4\n完整准备至发布跨度：%5\n累计活动耗时（不含暂停等待）：%6")
        .arg(boundary, Timestamp(t.startedUtc), CompletedTime(t), span,
            preparation ? span : QStringLiteral("未记录（开始范围不完整或未知）"), ActiveTime(t));
}
bool Viewable(const V::ExecutionSnapshot& s) {
    return s.state == V::ExecutionState::Completed && !s.active && !s.executionId.empty() &&
        !s.output.empty() && s.outputSha256.size() == 64 && s.resultGeneration && s.initialView.has_value();
}
QString State(const V::ExecutionSnapshot& s) {
    switch (s.state) {
    case V::ExecutionState::None: return QStringLiteral("尚未开始");
    case V::ExecutionState::Preparing: return QStringLiteral("准备中");
    case V::ExecutionState::Training: return QStringLiteral("训练中");
    case V::ExecutionState::Pausing: return QStringLiteral("正在暂停");
    case V::ExecutionState::Saving: return QStringLiteral("正在安全保存");
    case V::ExecutionState::Paused: return QStringLiteral("已安全暂停");
    case V::ExecutionState::Restoring: return QStringLiteral("正在恢复");
    case V::ExecutionState::Completed: return QStringLiteral("已完成");
    case V::ExecutionState::Failed: return QStringLiteral("失败");
    case V::ExecutionState::Stopped: return QStringLiteral("已停止（不代表已保存）");
    case V::ExecutionState::Interrupted: return QStringLiteral("已中断");
    }
    return QStringLiteral("未知状态");
}
QString Stage(const V::ExecutionSnapshot& s) {
    if (!s.active && s.state != V::ExecutionState::Preparing && s.state != V::ExecutionState::None) return State(s);
    if (s.state == V::ExecutionState::Pausing || s.state == V::ExecutionState::Saving) return State(s);
    // These are actual trusted candidate observations, not terminal publication.
    if (s.message == "Actual completion and finite Gaussian PLY validated; native retirement still required")
        return QStringLiteral("确认进程退出并发布结果");
    if (s.message == "Durable checkpoint validated; native retirement still required")
        return QStringLiteral("确认安全保存与进程退出");
    const auto& stage = s.stage;
    if (stage == "Environment admission") return QStringLiteral("检查运行环境");
    if (stage == "Import preflight") return QStringLiteral("检查保存空间与权限");
    if (stage == "Import video") return QStringLiteral("导入并校验视频");
    if (stage == "ffprobe") return QStringLiteral("读取视频信息");
    if (stage == "extract") return QStringLiteral("提取视频帧");
    if (stage == "features") return QStringLiteral("提取图像特征");
    if (stage == "matching") return QStringLiteral("匹配图像特征");
    if (stage == "mapping") return QStringLiteral("重建相机与稀疏结构");
    if (stage == "undistort") return QStringLiteral("校正图像畸变");
    if (stage.starts_with("convert-")) return QStringLiteral("整理相机与场景数据");
    if (stage == "brush") return s.state == V::ExecutionState::Restoring ? QStringLiteral("恢复训练检查点") : QStringLiteral("训练三维场景");
    if (stage.empty()) return s.state == V::ExecutionState::Restoring ? QStringLiteral("校验恢复条件") : s.state == V::ExecutionState::Preparing ? QStringLiteral("校验工具与输入") : QString{};
    return Text(stage);
}
bool SameResult(const V::ExecutionSnapshot& a, const V::ExecutionSnapshot& b) {
    return Viewable(a) && Viewable(b) && a.executionId == b.executionId &&
        a.resultGeneration == b.resultGeneration && a.outputSha256 == b.outputSha256 && a.output == b.output;
}
}

struct WorkbenchController::Impl {
    enum class Op { Initialize, Suggest, Open, Start, Resume, Select, Refresh, Theme, Root, Provision, Remove, Relocate, Stop };
    struct Command {
        Op op{Op::Initialize};
        std::uint64_t generation{};
        QString path, second, execution;
        WorkbenchNewTask task;
        int value{};
    };
    struct Event {
        Command command;
        bool final{true};
        QString error, warning, environment, directory, name, projectId;
        std::optional<LocalSettingsSnapshot> settings;
        std::optional<WorkbenchNewTask> suggestion;
        std::optional<V::ExecutionSnapshot> latest, selected;
        std::optional<std::chrono::steady_clock::time_point> preparationStarted;
        QList<WorkbenchExecutionRow> rows;
    };
    WorkbenchController& self;
    WorkbenchWindow& window;
    LocalSettings settings;
    QTimer poll;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Command> commands;
    std::deque<Event> events;
    std::shared_ptr<V::ProjectReconstruction> controls;
    bool workerDone{}, busy{true}, closing{}, exitAfterSave{}, forceExit{}, stopQueued{}, ownerStarted{};
    std::uint64_t generation{1}, retireToken{}, pendingRetire{}, requestId{};
    std::optional<Command> afterRetire;
    WorkbenchState state;
    std::optional<WorkbenchState> presented;
    V::ExecutionSnapshot latest;
    QString selectedExecution;
    std::string observedExecution;
    std::optional<std::chrono::steady_clock::time_point> preparationStarted;
    std::optional<StudioPreviewIdentity> accepted;
    std::optional<int> requestedTheme;

    Impl(WorkbenchController& object, WorkbenchWindow& view) : self(object), window(view) {
        auto connect = [this](auto signal, auto handler) { QObject::connect(&window, signal, &self, handler); };
        connect(&WorkbenchWindow::videoSelected, [this](const QString& p) {
            if (!Idle()) return;
            Command c; c.op = Op::Suggest; c.task = window.NewTask(); c.task.video = p; Submit(std::move(c));
        });
        connect(&WorkbenchWindow::startRequested, [this] {
            if (!Idle()) return;
            Command c; c.op = Op::Start; c.task = window.NewTask();
            if (c.task.profile < 0 || c.task.profile > 3 || c.task.name.trimmed().isEmpty() || c.task.root.isEmpty() ||
                (c.task.video.isEmpty() && state.projectPath.isEmpty()) || (c.task.profile == 3 && c.task.advancedConfig.isEmpty())) {
                Report(QStringLiteral("无法开始"), QStringLiteral("请选择一个视频、有效项目名称及位置；高级配置需要显式选择可信配置。")); return;
            }
            c.path = state.projectPath; BeginRetire(std::move(c));
        });
        connect(&WorkbenchWindow::openProjectRequested, [this](const QString& p) {
            if (!Idle() || p.isEmpty()) return;
            Command c; c.op = Op::Open; c.path = p; BeginRetire(std::move(c));
        });
        connect(&WorkbenchWindow::executionRequested, [this](const QString& id) {
            if (!Idle() || state.projectPath.isEmpty() || id.isEmpty()) return;
            Command c; c.op = Op::Select; c.execution = id; c.path = state.projectPath; BeginRetire(std::move(c));
        });
        connect(&WorkbenchWindow::resumeRequested, [this] {
            if (!Idle() || !latest.resumeAvailable) return;
            Command c; c.op = Op::Resume; c.path = state.projectPath; c.execution = Text(latest.executionId); BeginRetire(std::move(c));
        });
        connect(&WorkbenchWindow::pauseRequested, [this] { Pause(false); });
        connect(&WorkbenchWindow::forceStopRequested, [this] { Force(false); });
        connect(&WorkbenchWindow::backRequested, [this] { if (!closing) window.ShowWorkbench(); });
        connect(&WorkbenchWindow::retryPreviewRequested, [this] { Reload(); });
        connect(&WorkbenchWindow::refreshRequested, [this] { Reload(); });
        connect(&WorkbenchWindow::themeRequested, [this](int value) {
            if (closing || !worker.joinable() || value < 0 || value > 2) return;
            requestedTheme = value;
            Command c; c.op = Op::Theme; c.value = value;
            // Settings follow active work on the same worker, without changing
            // its project generation, busy state or preview retirement.
            {
                std::lock_guard lock(mutex);
                const auto pending = std::find_if(commands.begin(), commands.end(), [](const Command& queued) { return queued.op == Op::Theme; });
                if (pending != commands.end()) *pending = std::move(c);
                else commands.push_back(std::move(c));
            }
            wake.notify_one();
        });
        connect(&WorkbenchWindow::rootRequested, [this](const QString& path) {
            if (!Idle()) return;
            Command c; c.op = Op::Root; c.path = path; Submit(std::move(c));
        });
        connect(&WorkbenchWindow::provisionRuntimeRequested, [this](const QString& path) {
            if (!Idle()) return;
            Command c; c.op = Op::Provision; c.path = path; Submit(std::move(c));
        });
        connect(&WorkbenchWindow::removeRecentRequested, [this](const QString& path) {
            if (!Idle()) return;
            Command c; c.op = Op::Remove; c.path = path; Submit(std::move(c));
        });
        connect(&WorkbenchWindow::relocateRecentRequested, [this](const QString& path, const QString& next) {
            if (!Idle()) return;
            Command c; c.op = Op::Relocate; c.path = path; c.second = next; Submit(std::move(c));
        });
        connect(&WorkbenchWindow::closeRequested, [this] { Close(); });
        QObject::connect(window.Preview(), &NativePreviewWidget::statusChanged, &self, [this] { PreviewChanged(); });
        try { worker = std::thread([this] { Worker(); }); }
        catch (const std::exception& e) { workerDone = true; busy = false; state.detail = Text(e.what()); }
        poll.setInterval(33);
        QObject::connect(&poll, &QTimer::timeout, &self, [this] { Poll(); }); poll.start();
        if (worker.joinable()) { Command c; c.generation = generation; Enqueue(std::move(c)); }
        QTimer::singleShot(0, &self, [this] {
            if (closing) return;
            ownerStarted = window.Preview()->StartOwner();
            if (!ownerStarted) Report(QStringLiteral("预览不可用"), Text(window.Preview()->Snapshot().error));
        });
        Render();
    }
    ~Impl() {
        if (worker.joinable() || !window.Preview()->IsStopped())
            qFatal("WorkbenchController requires acknowledged asynchronous shutdown before destruction");
    }
    bool Idle() const { return !busy && !closing && !exitAfterSave && !pendingRetire && worker.joinable(); }
    void Report(const QString& title, const QString& message) {
        if (closing) return;
        // Background completions update this window without activating an OS dialog.
        state.detail += '\n' + title + QStringLiteral(": ") + message;
        if (window.isActiveWindow()) window.ShowError(title, message);
        else state.environment += '\n' + title + QStringLiteral(": ") + message;
        Render();
    }
    void Enqueue(Command c) {
        { std::lock_guard lock(mutex); commands.push_back(std::move(c)); } wake.notify_one();
    }
    void Post(Event e) { std::lock_guard lock(mutex); events.push_back(std::move(e)); }
    void Submit(Command c) {
        busy = true; c.generation = generation; Enqueue(std::move(c)); Render();
    }
    void BeginRetire(Command c) {
        busy = true; c.generation = ++generation; afterRetire = std::move(c); accepted.reset();
        observedExecution.clear();
        pendingRetire = ++retireToken; window.Preview()->Retire(pendingRetire);
        state.stage = QStringLiteral("释放预览资源");
        state.trainingPercent = -1; state.trainingText.clear();
        state.detail = QStringLiteral("正在释放旧场景 GPU 资源，确认后继续…"); Render();
    }
    void Reload() {
        if (!Idle()) return;
        if (state.projectPath.isEmpty()) { Command c; c.op = Op::Initialize; Submit(std::move(c)); return; }
        Command c; c.op = Op::Refresh; c.path = state.projectPath; c.execution = selectedExecution; BeginRetire(std::move(c));
    }
    std::shared_ptr<V::ProjectReconstruction> Service() { std::lock_guard lock(mutex); return controls; }
    void PublishService(const std::shared_ptr<V::ProjectReconstruction>& p) { std::lock_guard lock(mutex); controls = p; }

    void Worker() {
        std::unique_ptr<V::ReconstructionQueue> queue;
        std::shared_ptr<V::ProjectReconstruction> service;
        auto replace = [&] { PublishService({}); service.reset(); queue.reset(); };
        for (;;) {
            Command c;
            { std::unique_lock lock(mutex); wake.wait(lock, [this] { return !commands.empty(); }); c = std::move(commands.front()); commands.pop_front(); }
            if (c.op == Op::Stop) break;
            Event e; e.command = c;
            bool createdProject = false;
            std::optional<std::chrono::steady_clock::time_point> preparing;
            auto observe = [&](const V::ExecutionSnapshot& s) {
                Event update; update.command = c; update.final = false; update.latest = s;
                if (s.executionId.empty()) update.preparationStarted = preparing;
                if (queue) { update.directory = PathText(queue->Directory()); update.name = Text(queue->Snapshot().name); update.projectId = Text(queue->Snapshot().projectId); }
                Post(std::move(update));
            };
            auto warning = [&](const std::exception& error) { e.warning += Text(error.what()) + '\n'; };
            try {
                if (c.op == Op::Select || c.op == Op::Refresh || c.op == Op::Resume) {
                    std::error_code ec;
                    if (!queue || !fs::equivalent(queue->Directory(), Path(c.path), ec) || ec)
                        throw std::runtime_error("Command no longer matches the leased project");
                }
                switch (c.op) {
                case Op::Initialize:
                    e.settings = settings.Load();
                    try { e.environment = QStringLiteral("环境已校验：") + PathText(V::DiscoverRuntimeConfiguration()); }
                    catch (const std::exception& error) { e.environment = QStringLiteral("环境未就绪：") + Text(error.what()); }
                    break;
                case Op::Suggest: {
                    auto suggestion = c.task;
                    const auto stem = Path(suggestion.video).stem().wstring();
                    suggestion.name = QString::fromStdWString(stem).trimmed();
                    if (suggestion.name.isEmpty()) suggestion.name = QStringLiteral("新项目");
                    if (suggestion.root.isEmpty()) suggestion.root = settings.Load().projectRoot;
                    e.suggestion = UnusedName(suggestion); break;
                }
                case Op::Theme: settings.SaveTheme(static_cast<ThemePreference>(c.value)); e.settings = settings.Load(); break;
                case Op::Root: settings.SaveProjectRoot(c.path); e.settings = settings.Load(); break;
                case Op::Remove: settings.RemoveRecent(c.path); e.settings = settings.Load(); break;
                case Op::Relocate: settings.RelocateRecent(c.path, c.second); e.settings = settings.Load(); break;
                case Op::Provision:
                    V::ProvisionRuntimeConfiguration(Path(c.path));
                    e.environment = QStringLiteral("环境已校验：") + PathText(V::DiscoverRuntimeConfiguration()); break;
                case Op::Open: {
                    std::error_code ec;
                    const bool same = queue && fs::equivalent(queue->Directory(), Path(c.path), ec) && !ec;
                    if (!same) {
                        replace(); queue = V::ReconstructionQueue::OpenForInspection(Path(c.path));
                        service = std::make_shared<V::ProjectReconstruction>(*queue, fs::path{}); PublishService(service);
                    }
                    break;
                }
                case Op::Start: {
                    const auto started = std::chrono::steady_clock::now();
                    const auto startedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                    preparing = started;
                    V::ExecutionSnapshot admission; admission.state = V::ExecutionState::Preparing;
                    admission.stage = "Environment admission"; admission.message = "Validating pinned runtime and selected destination"; observe(admission);
                    const auto profile = static_cast<V::ReconstructionProfile>(c.task.profile);
                    const auto config = profile == V::ReconstructionProfile::Advanced ? Path(c.task.advancedConfig) : V::DiscoverRuntimeConfiguration();
                    const auto target = Path(c.task.root) / Path(c.task.name);
                    if (!Path(c.task.root).is_absolute()) throw std::runtime_error("Choose an absolute project root directory");
                    // Filename components only: never let an edited name escape its root.
                    if (Path(c.task.name).filename() != Path(c.task.name) || c.task.name == "." || c.task.name == "..")
                        throw std::runtime_error("Project name must be one filename component");
                    std::error_code ec;
                    const bool same = queue && fs::equivalent(queue->Directory(), target, ec) && !ec;
                    if (!same) {
                        if (fs::exists(target)) {
                            e.suggestion = UnusedName(c.task);
                            throw std::runtime_error("Project destination already exists. An unused name has been suggested; review the new target and Start again. No existing data was changed.");
                        }
                        if (!fs::is_regular_file(Path(c.task.video))) throw std::runtime_error("Choose one existing regular video file");
                        const auto size = fs::file_size(Path(c.task.video));
                        if (!size || size > V::kMaxInputFileBytes) throw std::runtime_error("Video is empty or exceeds the 16 GiB input limit");
                        replace();
                        fs::create_directories(Path(c.task.root));
                        if (!fs::create_directory(target)) {
                            e.suggestion = UnusedName(c.task);
                            throw std::runtime_error("Project destination was created by another operation. Review the suggested unused name and Start again; its data was not changed.");
                        }
                        queue = V::ReconstructionQueue::Create(target, Bytes(c.task.name));
                        createdProject = true;
                        V::ExecutionSnapshot preparation; preparation.state = V::ExecutionState::Preparing;
                        preparation.stage = "Import preflight"; preparation.message = "Checking owned destination before copying video"; observe(preparation);
                        V::SpaceBudget budget;
                        budget.mediaCopies = size; budget.preparedImages = 0; budget.reconstructionDatabase = 0;
                        budget.checkpoints = 0; budget.temporaryExports = 0; budget.lod = 0;
                        budget.newAssets = V::kMaxInputManifestBytes; budget.retainedOverlap = 0; budget.reserve = 0;
                        const auto space = V::QuerySpacePreflight(queue->Directory(), budget);
                        if (space.diagnostic.code != V::PreflightCode::Ok) throw std::runtime_error(space.diagnostic.message);
                        const auto write = V::ProbeDirectoryWrite(queue->Directory());
                        if (write.diagnostic.code != V::PreflightCode::Ok) throw std::runtime_error(write.diagnostic.message);
                        preparation.stage = "Import video"; preparation.message = "Copying and verifying project-owned video"; observe(preparation);
                        queue->ImportVideo(Path(c.task.video));
                    } else {
                        const auto input = queue->InspectInput();
                        std::error_code inputError;
                        if (input.kind != V::InputKind::Video || input.files.size() != 1 ||
                            !fs::equivalent(queue->Directory() / fs::u8path(input.files.front().relativePath), Path(c.task.video), inputError) || inputError) {
                            e.suggestion = UnusedName(c.task);
                            throw std::runtime_error("This destination is the current project, but the video is not its owned input. Select a new unused project name to import a different source.");
                        }
                    }
                    service = std::make_shared<V::ProjectReconstruction>(*queue, config, profile); PublishService(service);
                    service->SetPreparationTiming({static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()), Bytes(startedUtc)});
                    e.latest = service->RunNew(observe); break;
                }
                case Op::Resume:
                    if (!queue || !service || Text(service->Snapshot().executionId) != c.execution)
                        throw std::runtime_error("Resume request no longer matches the selected project execution");
                    // Resume reads its own immutable execution configuration, never future UI settings.
                    e.latest = service->Resume(observe); break;
                case Op::Select: case Op::Refresh:
                    if (!queue || !service) throw std::runtime_error("Open a project first");
                    break;
                case Op::Stop: break;
                }
                const bool projectOperation = c.op == Op::Open || c.op == Op::Start || c.op == Op::Resume || c.op == Op::Select || c.op == Op::Refresh;
                if (projectOperation && queue) {
                    if (!service) { service = std::make_shared<V::ProjectReconstruction>(*queue, fs::path{}); PublishService(service); }
                    // Inspect latest independently: selecting history must never replace task truth.
                    try { if (!e.latest) e.latest = service->Inspect(); } catch (const std::exception& error) {
                        V::ExecutionSnapshot failure; failure.state = V::ExecutionState::Failed; failure.message = error.what(); e.latest = failure;
                    }
                    std::vector<V::ExecutionSnapshot> history;
                    try { history = service->ListExecutions(); }
                    catch (const std::exception& error) { warning(error); }
                    std::stable_sort(history.begin(), history.end(), [](const auto& a, const auto& b) {
                        return a.timing.completedUtc.value_or("") > b.timing.completedUtc.value_or("");
                    });
                    for (const auto& s : history) e.rows.append({Text(s.executionId), State(s), Text(s.profileName), CompletedTime(s.timing), ActiveTime(s.timing), s.state == V::ExecutionState::Completed});
                    if (!c.execution.isEmpty() && (c.op == Op::Select || c.op == Op::Refresh)) {
                        auto selected = service->InspectExecution(Bytes(c.execution));
                        if (!Viewable(selected)) throw std::runtime_error("Selected execution has no verified output and reconstruction camera");
                        e.selected = selected;
                    } else if (c.op == Op::Start || c.op == Op::Resume) {
                        // Automatic navigation belongs exclusively to this run's published result.
                        // An older success remains explicitly selectable, never an automatic fallback.
                        if (e.latest && Viewable(*e.latest) && (c.op != Op::Resume || Text(e.latest->executionId) == c.execution)) {
                            try {
                                auto selected = service->InspectExecution(e.latest->executionId);
                                if (SameResult(*e.latest, selected)) e.selected = std::move(selected);
                            } catch (const std::exception& error) { warning(error); }
                        }
                    } else if (c.op == Op::Open || c.op == Op::Refresh) {
                        if (e.latest && Viewable(*e.latest)) {
                            try { auto selected = service->InspectExecution(e.latest->executionId); if (Viewable(selected)) e.selected = selected; }
                            catch (const std::exception& error) { warning(error); }
                        }
                        if (!e.selected) for (const auto& s : history) {
                            if (s.state != V::ExecutionState::Completed) continue;
                            try { auto selected = service->InspectExecution(s.executionId); if (Viewable(selected)) { e.selected = selected; break; } }
                            catch (const std::exception& error) { warning(error); }
                        }
                    }
                    if (c.op == Op::Open || c.op == Op::Start) {
                        try { settings.RecordOpened({PathText(queue->Directory()), Text(queue->Snapshot().name), Text(queue->Snapshot().projectId), e.latest ? State(*e.latest) : QString{}, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}); e.settings = settings.Load(); }
                        catch (const std::exception& error) { warning(error); }
                    }
                    if (c.op == Op::Open || c.op == Op::Start) {
                        try {
                            const auto input = queue->InspectInput();
                            if (input.kind == V::InputKind::Video && input.files.size() == 1) {
                                auto task = c.task;
                                task.video = PathText(queue->Directory() / fs::u8path(input.files.front().relativePath));
                                task.name = Text(queue->Snapshot().name); task.root = PathText(queue->Directory().parent_path());
                                e.suggestion = task;
                            }
                        } catch (const V::InputError& error) { if (error.Code() != V::InputErrorCode::NoInput) warning(error); }
                    }
                }
            } catch (const std::exception& error) {
                e.error = Text(error.what());
                if (c.op == Op::Theme || c.op == Op::Root) {
                    try { e.settings = settings.Load(); } catch (const std::exception& restoreError) { warning(restoreError); }
                }
            }
            catch (...) { e.error = QStringLiteral("操作遇到未知错误；原始输入和已创建项目保留。请检查环境与目录后重试。"); }
            if (createdProject && !e.error.isEmpty() && queue) {
                try {
                    settings.RecordOpened({PathText(queue->Directory()), Text(queue->Snapshot().name), Text(queue->Snapshot().projectId),
                        QStringLiteral("创建后操作失败；项目保留"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)});
                    e.settings = settings.Load();
                } catch (const std::exception& error) { warning(error); }
            }
            // Keep a partially created project's lease and data on import/admission failure.
            if (queue) { e.directory = PathText(queue->Directory()); e.name = Text(queue->Snapshot().name); e.projectId = Text(queue->Snapshot().projectId); }
            Post(std::move(e));
        }
        replace();
        { std::lock_guard lock(mutex); workerDone = true; }
    }

    void ApplySettings(const LocalSettingsSnapshot& s, bool applyRoot) {
        window.ApplyTheme(requestedTheme.value_or(static_cast<int>(s.theme)));
        if (applyRoot) window.SetProjectRoot(s.projectRoot);
        QList<WorkbenchProjectRow> rows;
        for (const auto& p : s.recentProjects) rows.append({p.directory, p.name, p.lastObservedState});
        window.SetRecentProjects(rows);
        if (!s.warning.isEmpty()) Report(QStringLiteral("设置未能读取；原文件已保留"), s.warning);
    }
    void Render() {
        state.busy = busy; state.closing = closing || exitAfterSave;
        state.pauseAvailable = busy && latest.pauseAvailable && !latest.controlPending && !exitAfterSave;
        state.resumeAvailable = !busy && latest.resumeAvailable;
        state.controlPending = latest.controlPending || exitAfterSave;
        if (presented) {
            const auto& p = *presented;
            if (state.projectName == p.projectName && state.projectPath == p.projectPath && state.status == p.status &&
                state.stage == p.stage && state.detail == p.detail && state.activeTime == p.activeTime &&
                state.completedAt == p.completedAt && state.trainingText == p.trainingText && state.environment == p.environment &&
                state.busy == p.busy && state.pauseAvailable == p.pauseAvailable && state.resumeAvailable == p.resumeAvailable &&
                state.controlPending == p.controlPending && state.closing == p.closing && state.trainingPercent == p.trainingPercent) return;
        }
        presented = state;
        window.ApplyState(state);
    }
    void ApplyLatest(const V::ExecutionSnapshot& s) {
        latest = s; state.status = State(s); state.stage = Stage(s); state.detail = Text(s.message);
        if (!s.stage.empty()) state.detail += QStringLiteral("\n阶段事件：") + Text(s.stage);
        state.activeTime = ActiveTime(s.timing); state.completedAt = CompletedTime(s.timing);
        state.trainingPercent = -1; state.trainingText.clear();
        if (s.progress.completedIterations && s.progress.totalIterations && *s.progress.totalIterations) {
            const auto n = std::min(*s.progress.completedIterations, *s.progress.totalIterations);
            state.trainingText = QStringLiteral("训练迭代 %1 / %2").arg(n).arg(*s.progress.totalIterations);
            state.trainingPercent = static_cast<int>(100.0L * n / *s.progress.totalIterations);
        }
        Render();
    }
    void LoadResult(const Event& e) {
        if (!e.final || e.command.generation != generation || !e.selected || !Viewable(*e.selected) || closing || exitAfterSave || busy) return;
        if (e.command.op == Op::Start || e.command.op == Op::Resume) {
            if (!e.latest || !SameResult(*e.latest, *e.selected) || !SameResult(latest, *e.selected) ||
                observedExecution != e.selected->executionId ||
                (e.command.op == Op::Resume && e.command.execution != Text(e.selected->executionId))) return;
        } else if (e.command.op != Op::Open && e.command.op != Op::Select && e.command.op != Op::Refresh) return;
        const auto& s = *e.selected;
        StudioPreviewRequest request;
        request.identity = {Path(state.projectPath), generation, s.executionId, ++requestId, s.resultGeneration, s.outputSha256};
        request.output = s.output; request.initialView = s.initialView;
        selectedExecution = Text(s.executionId); accepted = request.identity;
        window.ShowResult(state.projectName, QStringLiteral("执行：%1\n配置：%2\n%3\n输出 SHA256：%4\n%5")
            .arg(selectedExecution, Text(s.profileName), TimingDetails(s.timing), Text(s.outputSha256), Text(s.message)));
        window.SetResultTiming(CompletedTime(s.timing), ActiveTime(s.timing));
        if (!ownerStarted || !window.Preview()->Load(std::move(request))) {
            accepted.reset(); Report(QStringLiteral("预览加载失败"), QStringLiteral("业务结果仍保留；返回工作台或重试预览。"));
        }
    }
    void PreviewChanged() {
        if (closing || !accepted) return;
        const auto p = window.Preview()->Snapshot(); const auto& id = p.identity;
        if (id.projectGeneration != generation || id.requestId != accepted->requestId || id.executionId != accepted->executionId ||
            id.resultGeneration != accepted->resultGeneration || id.contentHash != accepted->contentHash || id.projectDirectory != accepted->projectDirectory) return;
        if (p.state == StudioPreviewState::Failed) {
            accepted.reset(); Report(QStringLiteral("预览失败（重建结果保留）"), Text(p.error));
        }
    }
    void Pause(bool forClose) {
        if (closing || !busy || latest.controlPending || !latest.pauseAvailable) return;
        const auto service = Service();
        if (!service || !service->RequestPause()) {
            if (forClose) exitAfterSave = false;
            Report(QStringLiteral("暂时无法安全暂停"), QStringLiteral("训练控制尚未就绪，请继续等待后重试。")); return;
        }
        latest.controlPending = true; exitAfterSave = forClose; Render();
    }
    void Force(bool forClose) {
        if (closing || !busy || latest.controlPending) return;
        QMessageBox dialog(QMessageBox::Warning, QStringLiteral("强制停止"), QStringLiteral("强制停止可能丢失尚未保存的训练进度。继续运行是默认操作。"), QMessageBox::NoButton, &window);
        auto* keep = dialog.addButton(QStringLiteral("继续运行"), QMessageBox::RejectRole);
        auto* stop = dialog.addButton(QStringLiteral("强制停止"), QMessageBox::DestructiveRole); dialog.setDefaultButton(keep); dialog.exec();
        if (dialog.clickedButton() != stop || closing || !busy) return;
        const auto service = Service();
        if (service && service->RequestForceStop()) { forceExit = forClose; latest.controlPending = true; Render(); }
        else Report(QStringLiteral("当前无法强制停止"), QStringLiteral("准备或文件操作尚在进行，请等待当前步骤完成。"));
    }
    void Close() {
        if (closing || exitAfterSave) return;
        if (busy || pendingRetire) {
            QMessageBox dialog(QMessageBox::Question, QStringLiteral("任务仍在运行"), QStringLiteral("默认继续运行。只有真实训练控制可用时才能安全保存后退出。准备、导入和校验期间请等待当前操作。"), QMessageBox::NoButton, &window);
            auto* keep = dialog.addButton(QStringLiteral("继续运行"), QMessageBox::RejectRole);
            QPushButton* save = nullptr; QPushButton* stop = nullptr;
            if (latest.pauseAvailable && !latest.controlPending) save = dialog.addButton(QStringLiteral("安全保存后退出"), QMessageBox::AcceptRole);
            if (latest.active && !latest.controlPending) stop = dialog.addButton(QStringLiteral("强制停止并退出…"), QMessageBox::DestructiveRole);
            dialog.setDefaultButton(keep); dialog.exec();
            if (save && dialog.clickedButton() == save) Pause(true);
            else if (stop && dialog.clickedButton() == stop) Force(true);
            return;
        }
        FinalClose();
    }
    void FinalClose() {
        if (closing || busy || pendingRetire) return;
        closing = true; exitAfterSave = false; ++generation; accepted.reset(); afterRetire.reset();
        window.Preview()->RequestStop(); Render();
    }
    void Poll() {
        std::deque<Event> pending; bool done;
        { std::lock_guard lock(mutex); pending.swap(events); done = workerDone; }
        for (const auto& e : pending) {
            if (e.command.op == Op::Theme) {
                if (closing) continue;
                if (requestedTheme && *requestedTheme == e.command.value) {
                    requestedTheme.reset();
                    if (e.settings) window.ApplyTheme(static_cast<int>(e.settings->theme));
                }
                if (!e.error.isEmpty()) Report(QStringLiteral("主题设置保存失败"), e.error);
                continue;
            }
            if (closing || e.command.generation != generation) continue;
            if (!e.directory.isEmpty()) {
                if (!state.projectPath.isEmpty() && state.projectPath != e.directory && e.command.op != Op::Open && e.command.op != Op::Start) continue;
                state.projectPath = e.directory; state.projectName = e.name;
            }
            if (e.latest) {
                // Once a run has an ID, observations must remain in that lineage.
                if (!e.latest->executionId.empty()) {
                    if (!observedExecution.empty() && observedExecution != e.latest->executionId) continue;
                    observedExecution = e.latest->executionId;
                }
                ApplyLatest(*e.latest);
                preparationStarted = e.preparationStarted;
                if (exitAfterSave && !e.latest->controlPending && e.latest->active && e.latest->state != V::ExecutionState::Pausing && e.latest->state != V::ExecutionState::Saving) {
                    exitAfterSave = false;
                    Report(QStringLiteral("安全保存未完成，窗口保持打开"), Text(e.latest->message));
                }
            }
            if (!e.final) continue;
            busy = false;
            preparationStarted.reset();
            if (e.settings) ApplySettings(*e.settings, e.command.op == Op::Initialize || e.command.op == Op::Root);
            if (!e.environment.isEmpty()) state.environment = e.environment;
            Render();
            if (e.suggestion) window.SetSuggestedTask(*e.suggestion);
            if (e.command.op == Op::Open || e.command.op == Op::Start || e.command.op == Op::Resume || e.command.op == Op::Select || e.command.op == Op::Refresh) {
                Render(); // Bind history rows to the current project before setting rows.
                window.SetExecutions(e.rows);
            }
            if (!e.error.isEmpty()) {
                state.detail = e.error;
                if (!e.latest && (e.command.op == Op::Start || e.command.op == Op::Resume)) { latest.state = V::ExecutionState::Failed; latest.active = false; latest.controlPending = false; latest.pauseAvailable = false; state.status = QStringLiteral("失败"); }
                exitAfterSave = false; forceExit = false;
                Report(QStringLiteral("操作失败；现有数据保留"), e.error);
            }
            if (!e.warning.isEmpty()) Report(QStringLiteral("部分操作未完成（项目与结果保留）"), e.warning);
            Render();
            if (exitAfterSave || forceExit) {
                const bool saved = e.latest && (e.latest->state == V::ExecutionState::Paused || e.latest->state == V::ExecutionState::Completed);
                if (forceExit || saved) { forceExit = false; FinalClose(); continue; }
                exitAfterSave = false; Render();
                Report(QStringLiteral("保存未确认，窗口保持打开"), state.detail);
            }
            if (e.error.isEmpty()) LoadResult(e);
        }
        if (!closing && busy && latest.active && !pendingRetire) {
            if (const auto service = Service()) {
                const auto live = service->Snapshot();
                if (live.executionId == latest.executionId && live.active) {
                    state.activeTime = ActiveTime(live.timing);
                }
            }
        }
        if (!closing && busy && preparationStarted && latest.executionId.empty()) {
            V::ExecutionTiming timing;
            timing.activeMilliseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *preparationStarted).count());
            timing.activeTimeComplete = true; state.activeTime = ActiveTime(timing);
        }
        if (!closing && pendingRetire && window.Preview()->Snapshot().retiredToken >= pendingRetire) {
            pendingRetire = 0;
            if (afterRetire) {
                auto c = std::move(*afterRetire); afterRetire.reset();
                if (c.op == Op::Start) { latest = {}; selectedExecution.clear(); }
                if (c.op == Op::Open) { latest = {}; selectedExecution.clear(); state.projectPath.clear(); state.projectName.clear(); }
                Enqueue(std::move(c));
            }
        }
        if (closing && window.Preview()->IsStopped() && !stopQueued) {
            stopQueued = true; Command c; c.op = Op::Stop; Enqueue(std::move(c));
        }
        if (closing && done && window.Preview()->IsStopped()) {
            if (worker.joinable()) worker.join();
            poll.stop(); window.PermitClose(); return;
        }
        Render();
    }
};

WorkbenchController::WorkbenchController(WorkbenchWindow& window, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, window)) {}
WorkbenchController::~WorkbenchController() = default;
}
