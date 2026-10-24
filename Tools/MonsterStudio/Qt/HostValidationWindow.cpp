#include "HostValidationWindow.h"
#include <QCloseEvent>
#include <QDateTime>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>
#include <stdexcept>

namespace MonsterStudioQt {
namespace {
QString Text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
QString PathText(const std::filesystem::path& value) { return QString::fromStdWString(value.wstring()); }
std::filesystem::path Path(const QString& value) { return std::filesystem::path(value.toStdWString()); }
QString StateName(StudioPreviewState state) {
    switch (state) {
    case StudioPreviewState::Unloaded: return QStringLiteral("未加载");
    case StudioPreviewState::Loading: return QStringLiteral("正在加载真实场景");
    case StudioPreviewState::Ready: return QStringLiteral("已提交真实场景帧");
    case StudioPreviewState::Failed: return QStringLiteral("预览失败");
    case StudioPreviewState::Retiring: return QStringLiteral("正在退休渲染资源");
    case StudioPreviewState::Stopped: return QStringLiteral("渲染所有者已停止");
    }
    return QStringLiteral("未知状态");
}
QByteArray Diagnostic(const StudioPreviewStatus& value, std::uint64_t generation,
                      const QSize& logicalSize, qreal scale) {
    QJsonObject identity{{"projectDirectory", PathText(value.identity.projectDirectory)},
        {"projectGeneration", QString::number(value.identity.projectGeneration)},
        {"executionId", Text(value.identity.executionId)},
        {"requestId", QString::number(value.identity.requestId)},
        {"resultGeneration", QString::number(value.identity.resultGeneration)},
        {"contentHash", Text(value.identity.contentHash)}};
    QJsonObject object{{"application", "MonsterStudio Qt stage-1 validation"},
        {"observedUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"generation", QString::number(generation)}, {"identity", identity},
        {"state", StateName(value.state)}, {"error", Text(value.error)},
        {"childWindow", QString::number(static_cast<qulonglong>(value.childWindow))},
        {"deviceName", Text(value.deviceName)}, {"physicalWidth", static_cast<int>(value.width)},
        {"physicalHeight", static_cast<int>(value.height)}, {"logicalWidth", logicalSize.width()},
        {"logicalHeight", logicalSize.height()}, {"devicePixelRatio", scale},
        {"submittedFrames", QString::number(value.submittedFrames)},
        {"lastSubmissionToken", QString::number(value.lastSubmissionToken)},
        {"lastPresentResult", value.lastPresentResult},
        {"lastPresentReturnNanoseconds", QString::number(value.lastPresentReturnNanoseconds)},
        {"deviceCreations", QString::number(value.deviceCreations)},
        {"deviceDestructions", QString::number(value.deviceDestructions)},
        {"sessionShutdowns", QString::number(value.sessionShutdowns)},
        {"retiredToken", QString::number(value.retiredToken)},
        {"reservedBytesAfterRetire", QString::number(value.reservedBytesAfterRetire)},
        {"ownerDiagnosticsJSON", Text(value.diagnosticsJSON)},
        {"presentationEvidence", "Owner capture is not visible-window presentation evidence"}};
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}
}

HostValidationWindow::HostValidationWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName("hostValidationWindow");
    setWindowTitle(QStringLiteral("MonsterStudio · 第一阶段 Qt / Vulkan 宿主验证"));
    resize(1120, 800);
    auto* body = new QWidget(this);
    auto* layout = new QVBoxLayout(body);
    auto* heading = new QLabel(QStringLiteral("第一阶段验证窗口 · 非完整工作台"), body);
    heading->setStyleSheet("font-size:18px; font-weight:600;");
    layout->addWidget(heading);
    auto* note = new QLabel(QStringLiteral("仅查看已成功项目：串行校验、原生 Vulkan 预览和生命周期验证。拖动旋转，滚轮缩放，R 复位。"), body);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* input = new QHBoxLayout;
    project_ = new QLineEdit(body);
    project_->setObjectName("projectDirectory");
    project_->setAccessibleName(QStringLiteral("已有项目目录"));
    project_->setPlaceholderText(QStringLiteral("选择已有成功项目的目录，不接受单独 PLY 文件"));
    browse_ = new QPushButton(QStringLiteral("选择项目…"), body);
    browse_->setObjectName("browseProject");
    load_ = new QPushButton(QStringLiteral("校验并加载"), body);
    load_->setObjectName("loadProject");
    input->addWidget(project_, 1); input->addWidget(browse_); input->addWidget(load_);
    layout->addLayout(input);
    auto* actions = new QHBoxLayout;
    retire_ = new QPushButton(QStringLiteral("卸载并释放项目"), body);
    retire_->setObjectName("retirePreview");
    reset_ = new QPushButton(QStringLiteral("复位视角"), body);
    reset_->setObjectName("resetView");
    export_ = new QPushButton(QStringLiteral("导出本地诊断…"), body);
    export_->setObjectName("exportDiagnostics");
    actions->addWidget(retire_); actions->addWidget(reset_); actions->addWidget(export_); actions->addStretch();
    layout->addLayout(actions);
    status_ = new QLabel(QStringLiteral("请选择已有成功项目。"), body);
    status_->setObjectName("hostStatus"); status_->setWordWrap(true);
    layout->addWidget(status_);
    preview_ = new NativePreviewWidget(body);
    preview_->setObjectName("nativePreview");
    preview_->setAccessibleName(QStringLiteral("原生三维结果预览"));
    preview_->setToolTip(QStringLiteral("鼠标左键拖动旋转，滚轮缩放，R 或 Home 复位；方向键可旋转。"));
    preview_->setMinimumSize(320, 200);
    layout->addWidget(preview_, 1);
    details_ = new QPlainTextEdit(body);
    details_->setObjectName("previewDetails"); details_->setReadOnly(true);
    details_->setAccessibleName(QStringLiteral("真实预览状态与身份"));
    details_->setMaximumHeight(140);
    layout->addWidget(details_);
    activity_ = new QLabel(body); activity_->setObjectName("guiHeartbeat");
    layout->addWidget(activity_);
    setCentralWidget(body);
    connect(browse_, &QPushButton::clicked, this, &HostValidationWindow::ChooseProject);
    connect(load_, &QPushButton::clicked, this, &HostValidationWindow::BeginOpen);
    connect(project_, &QLineEdit::returnPressed, this, &HostValidationWindow::BeginOpen);
    connect(retire_, &QPushButton::clicked, this, [this] {
        if (!busy_ && !closing_) { ++generation_; failure_.clear(); BeginRetire(Command{Operation::Release, generation_}); }
    });
    connect(reset_, &QPushButton::clicked, preview_, &NativePreviewWidget::ResetView);
    connect(export_, &QPushButton::clicked, this, &HostValidationWindow::ExportDiagnostics);
    connect(preview_, &NativePreviewWidget::statusChanged, this, &HostValidationWindow::RefreshPreview);
    try { worker_ = std::thread(&HostValidationWindow::WorkerLoop, this); }
    catch (const std::exception& error) {
        workerDone_ = true;
        SetError(QStringLiteral("无法启动串行服务线程：") + Text(error.what()));
    }
    poll_.setInterval(33);
    connect(&poll_, &QTimer::timeout, this, &HostValidationWindow::Poll);
    poll_.start();
    UpdateControls();
    QTimer::singleShot(0, this, [this] {
        if (closing_) return;
        ownerStarted_ = preview_->StartOwner();
        if (!ownerStarted_) SetError(QStringLiteral("无法启动原生预览所有者：") + Text(preview_->Snapshot().error));
        UpdateControls();
    });
}

HostValidationWindow::~HostValidationWindow() {
    if (worker_.joinable() || !preview_->IsStopped())
        qFatal("HostValidationWindow destruction requires asynchronous preview and worker shutdown");
}

void HostValidationWindow::Enqueue(Command command) {
    { std::lock_guard lock(mutex_); commands_.push_back(std::move(command)); }
    wake_.notify_one();
}

void HostValidationWindow::WorkerLoop() {
    using namespace MonsterEngine::VideoTo3D;
    std::unique_ptr<ReconstructionQueue> queue;
    std::unique_ptr<ProjectReconstruction> service;
    for (;;) {
        Command command;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return !commands_.empty(); });
            command = std::move(commands_.front()); commands_.pop_front();
        }
        if (command.operation == Operation::Stop) break;
        Completion completion;
        completion.operation = command.operation; completion.generation = command.generation;
        completion.path = command.path;
        try {
            if (command.operation == Operation::Open) {
                // The GUI dispatches this only after native retirement acknowledgement.
                service.reset(); queue.reset();
                queue = ReconstructionQueue::OpenForInspection(command.path);
#ifdef MONSTER_STUDIO_QT_TESTING
                // Hold the real queue lease while a verification-only gate
                // observes in-flight work. Never hold the GUI/worker mutex
                // while waiting at the gate; close must remain asynchronous.
                std::function<void()> afterLease;
                { std::lock_guard lock(mutex_); afterLease = afterInspectionLeaseAcquiredForTest_; }
                if (afterLease) afterLease();
#endif
                service = std::make_unique<ProjectReconstruction>(*queue, std::filesystem::path{});
                const auto execution = service->Inspect();
                if (execution.state != ExecutionState::Completed || execution.active || execution.output.empty() ||
                    execution.outputSha256.empty() || execution.executionId.empty() || !execution.resultGeneration || !execution.initialView)
                    throw std::runtime_error("No verified Completed result with validated reconstruction camera is available");
                completion.request.identity = {queue->Directory(), command.generation, execution.executionId,
                    command.generation, execution.resultGeneration, execution.outputSha256};
                completion.request.output = execution.output;
                completion.request.initialView = execution.initialView;
            } else if (command.operation == Operation::Release) {
                service.reset(); queue.reset();
            } else if (command.operation == Operation::Export) {
                // Evidence creation and publication stay on this same serial worker.
                if (!std::filesystem::create_directory(command.path))
                    throw std::runtime_error("Diagnostic evidence directory already exists");
                QSaveFile file(PathText(command.path / L"host-observation.json"));
                if (!file.open(QIODevice::WriteOnly) || file.write(command.diagnostic) != command.diagnostic.size() || !file.commit())
                    throw std::runtime_error("Cannot publish host diagnostics: " + file.errorString().toStdString());
            }
        } catch (const std::exception& error) {
            completion.error = error.what();
            if (command.operation == Operation::Open) { service.reset(); queue.reset(); }
        } catch (...) {
            completion.error = "Unexpected service worker failure";
            if (command.operation == Operation::Open) { service.reset(); queue.reset(); }
        }
        { std::lock_guard lock(mutex_); completions_.push_back(std::move(completion)); }
    }
    service.reset(); queue.reset();
    { std::lock_guard lock(mutex_); workerDone_ = true; }
}

void HostValidationWindow::ChooseProject() {
    if (busy_ || closing_) return;
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择已有成功项目"), project_->text());
    if (!closing_ && !directory.isEmpty()) project_->setText(directory);
}

void HostValidationWindow::BeginOpen() {
    if (busy_ || closing_ || !ownerStarted_ || preview_->IsStopped() || !worker_.joinable()) return;
    if (project_->text().trimmed().isEmpty()) { SetError(QStringLiteral("请先选择项目目录。")); return; }
    failure_.clear(); ++generation_;
    BeginRetire(Command{Operation::Open, generation_, Path(project_->text())});
}

void HostValidationWindow::BeginRetire(std::optional<Command> next) {
    busy_ = true;
    afterRetire_ = std::move(next);
    pendingRetire_ = ++retireToken_;
    status_->setText(QStringLiteral("正在退休旧场景；确认后才释放项目或校验新结果…"));
    preview_->Retire(pendingRetire_);
    UpdateControls();
}

void HostValidationWindow::SetError(const QString& message) {
    failure_ = message;
    status_->setText(QStringLiteral("失败：") + failure_);
    status_->setStyleSheet("color:#bf3434;");
}

void HostValidationWindow::RefreshPreview() {
    const auto value = preview_->Snapshot();
    details_->setPlainText(QStringLiteral("状态：%1 | 请求：%2 | 执行：%3\n设备：%4 | 原生像素：%5 × %6 | 已提交帧：%7 | 退休确认：%8\n输出 SHA256：%9\n设备创建/销毁：%10/%11 | 退休后账户保留：%12 bytes")
        .arg(StateName(value.state)).arg(value.identity.requestId).arg(Text(value.identity.executionId))
        .arg(Text(value.deviceName)).arg(value.width).arg(value.height).arg(value.submittedFrames)
        .arg(value.retiredToken).arg(Text(value.identity.contentHash)).arg(value.deviceCreations)
        .arg(value.deviceDestructions).arg(value.reservedBytesAfterRetire));
    if (closing_) return;
    if (value.state == StudioPreviewState::Failed) SetError(Text(value.error));
    else if (!busy_ && failure_.isEmpty()) {
        status_->setStyleSheet({}); status_->setText(StateName(value.state));
    }
    UpdateControls();
}

void HostValidationWindow::UpdateControls() {
    const auto state = preview_->Snapshot().state;
    const bool idle = !busy_ && !closing_ && !pendingCapture_ && worker_.joinable();
    project_->setEnabled(idle); browse_->setEnabled(idle);
    load_->setEnabled(idle && ownerStarted_ && !preview_->IsStopped());
    retire_->setEnabled(idle && ownerStarted_ && !preview_->IsStopped());
    reset_->setEnabled(idle && state == StudioPreviewState::Ready);
    export_->setEnabled(idle && !pendingCapture_);
}

void HostValidationWindow::ExportDiagnostics() {
    if (busy_ || closing_ || pendingCapture_ || !worker_.joinable()) return;
    const auto directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择本地诊断保存目录"));
    if (closing_ || directory.isEmpty()) return;
    const auto stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz");
    Command command{Operation::Export, generation_, Path(directory) / Path(QStringLiteral("qt-host-") + stamp)};
    command.diagnostic = Diagnostic(preview_->Snapshot(), generation_, preview_->size(), preview_->devicePixelRatioF());
    busy_ = true; failure_.clear(); status_->setText(QStringLiteral("正在保存本地诊断…"));
    Enqueue(std::move(command)); UpdateControls();
}

void HostValidationWindow::Poll() {
    ++heartbeat_;
    activity_->setText(QStringLiteral("界面事件循环：%1 · GPU 帧提交与实屏呈现需分别验证").arg(heartbeat_));
    std::deque<Completion> completions;
    bool done;
    { std::lock_guard lock(mutex_); completions.swap(completions_); done = workerDone_; }
    for (auto& completion : completions) {
        if (closing_ || completion.generation != generation_) continue;
        busy_ = false;
        if (!completion.error.empty()) { SetError(Text(completion.error)); continue; }
        if (completion.operation == Operation::Open) {
            completion.request.identity.requestId = ++requestId_;
            if (!preview_->Load(std::move(completion.request))) SetError(QStringLiteral("原生预览拒绝加载；项目保留至卸载或关闭。"));
            else { failure_.clear(); status_->setStyleSheet({}); status_->setText(QStringLiteral("校验通过，正在加载真实结果…")); }
        } else if (completion.operation == Operation::Release) {
            status_->setStyleSheet({}); status_->setText(QStringLiteral("场景已退休，项目租约已释放。"));
        } else if (completion.operation == Operation::Export) {
            const auto value = preview_->Snapshot();
            if (value.state == StudioPreviewState::Ready) {
                const auto id = ++captureId_;
                if (preview_->RequestCapture(id, completion.path / L"owner-frame.png", completion.path / L"owner-frame.json")) {
                    pendingCapture_ = id;
                    status_->setText(QStringLiteral("诊断已保存，正在请求所有者帧捕获：") + PathText(completion.path));
                } else SetError(QStringLiteral("诊断已保存，但所有者帧捕获被拒绝：") + PathText(completion.path));
            } else status_->setText(QStringLiteral("诊断已保存（当前无可捕获场景）：") + PathText(completion.path));
        }
    }
    if (!closing_ && pendingRetire_ && preview_->Snapshot().retiredToken >= pendingRetire_) {
        pendingRetire_ = 0;
        if (afterRetire_) {
            auto command = std::move(*afterRetire_); afterRetire_.reset();
            status_->setText(command.operation == Operation::Open ? QStringLiteral("正在工作线程校验项目、输出和相机…") : QStringLiteral("正在释放项目租约…"));
            Enqueue(std::move(command));
        }
    }
    if (!closing_ && pendingCapture_) {
        const auto value = preview_->Snapshot();
        if (value.captureId == pendingCapture_ && !value.capturePending) {
            pendingCapture_ = 0;
            if (value.captureSucceeded) status_->setText(QStringLiteral("所有者帧与诊断已保存；实屏截图需单独采集。"));
            else SetError(QStringLiteral("所有者帧捕获失败：") + Text(value.captureError));
        }
    }
    if (closing_ && preview_->IsStopped() && !stopQueued_) {
        stopQueued_ = true; Enqueue(Command{Operation::Stop, generation_});
    }
    if (closing_ && done && preview_->IsStopped()) {
        if (worker_.joinable()) worker_.join(); // done acknowledgement: no service or disk work remains.
        closeReady_ = true; poll_.stop(); close(); return;
    }
    UpdateControls();
}

void HostValidationWindow::closeEvent(QCloseEvent* event) {
    if (closeReady_) { event->accept(); return; }
    event->ignore();
    if (closing_) return;
    closing_ = true; ++generation_; afterRetire_.reset();
    status_->setStyleSheet({}); status_->setText(QStringLiteral("正在停止渲染所有者；随后释放项目并关闭窗口…"));
    preview_->RequestStop();
    UpdateControls();
}
} // namespace MonsterStudioQt
