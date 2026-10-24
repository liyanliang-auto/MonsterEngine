#include "NativePreviewWidget.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QThread>
#include <QWheelEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
#include <utility>

namespace MonsterStudioQt {
#ifdef MONSTER_STUDIO_QT_TESTING
std::function<std::unique_ptr<StudioPreviewHost>(HWND)>
    NativePreviewWidget::ownerFactoryForTesting_;
#endif
namespace {
bool SameIdentity(const StudioPreviewIdentity& a, const StudioPreviewIdentity& b) {
    return a.projectDirectory == b.projectDirectory &&
        a.projectGeneration == b.projectGeneration && a.executionId == b.executionId &&
        a.requestId == b.requestId && a.resultGeneration == b.resultGeneration &&
        a.contentHash == b.contentHash;
}
}

NativePreviewWidget::NativePreviewWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("三维结果视口"));
    setToolTip(QStringLiteral("左键旋转，滚轮缩放，R 复位"));
    cover_ = new QWidget(this);
    cover_->setAttribute(Qt::WA_NativeWindow);
    cover_->setAutoFillBackground(true);
    cover_->setBackgroundRole(QPalette::Window);
    auto* coverLayout = new QVBoxLayout(cover_);
    coverLayout->addStretch();
    coverText_ = new QLabel(cover_);
    coverText_->setAlignment(Qt::AlignCenter);
    coverText_->setWordWrap(true);
    coverText_->setTextFormat(Qt::PlainText);
    coverLayout->addWidget(coverText_);
    coverLayout->addStretch();
    connect(this, &NativePreviewWidget::statusChanged, this, &NativePreviewWidget::UpdateCover);
    UpdateCover();
    poll_.setInterval(33);
    connect(&poll_, &QTimer::timeout, this, &NativePreviewWidget::Poll);
}

void NativePreviewWidget::UpdateCover() {
    // Creating the native child can synchronously show/resize its parent before
    // the constructor has installed the label.
    if (!cover_ || !coverText_) return;
    const bool ready = CanInteract();
    cover_->setVisible(!ready);
    if (ready) return;
    QString message;
    switch (snapshot_.state) {
    case StudioPreviewState::Unloaded: message = QStringLiteral("尚未加载三维结果\n请打开项目或选择已完成的执行。"); break;
    case StudioPreviewState::Loading: message = QStringLiteral("正在加载三维结果\n等待模型与 GPU 资源准备完成。"); break;
    case StudioPreviewState::Failed: message = QStringLiteral("三维结果加载失败\n已有重建结果仍保留，可重新加载或返回任务。"); break;
    case StudioPreviewState::Retiring: message = QStringLiteral("正在释放预览资源\n请等待资源交接完成。"); break;
    case StudioPreviewState::Stopped: message = QStringLiteral("预览已停止"); break;
    case StudioPreviewState::Ready: message = QStringLiteral("正在切换结果"); break;
    }
    coverText_->setText(message);
    cover_->setGeometry(rect());
    cover_->raise();
    // A native child has its own backing store; an alien QWidget painted over a
    // WA_PaintOnScreen parent would not reliably cover a Win32 Vulkan surface.
    if (cover_->internalWinId())
        ::SetWindowPos(reinterpret_cast<HWND>(cover_->internalWinId()), HWND_TOP,
                       0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

NativePreviewWidget::~NativePreviewWidget() {
    // A destructor cannot defer QWidget's HWND destruction. Enforce the supported
    // ownership contract rather than joining GPU work on the GUI thread or letting
    // the renderer access a destroyed HWND. Normal shell close is asynchronous.
    if (owner_ && !owner_->IsStopped())
        qFatal("NativePreviewWidget destroyed before asynchronous stop acknowledgement");
    poll_.stop();
    owner_.reset();
}

bool NativePreviewWidget::StartOwner() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (closing_ || stopAcknowledged_) return false;
    if (owner_) return SurfaceValid();
    surface_ = reinterpret_cast<HWND>(winId());
    if (!SurfaceValid()) { Fail("Cannot create a valid native preview surface"); return false; }
    try {
        // Existing Notify checks both target and message before PostMessageW.
        // Polling avoids receiver HWND lifetime and queued native notifications.
#ifdef MONSTER_STUDIO_QT_TESTING
        if (ownerFactoryForTesting_) owner_ = ownerFactoryForTesting_(surface_);
        else
#endif
            owner_ = std::make_unique<StudioPreviewHost>(surface_, nullptr, 0);
        if (!owner_) { Fail("Cannot start native preview owner"); return false; }
        const auto sequence = snapshot_.sequence + 1;
        const auto retiredToken = snapshot_.retiredToken;
        snapshot_ = owner_->Snapshot();
        snapshot_.sequence = sequence;
        // A retry must retain earlier empty-boundary acknowledgements. The new
        // owner must also observe that watermark before accepting a load.
        snapshot_.retiredToken = std::max(snapshot_.retiredToken, retiredToken);
        if (retireToken_) owner_->RequestRetire(retireToken_);
        ownerSequence_ = 0;
        surfaceSynchronized_ = false;
        SynchronizeSurface();
        poll_.start();
        emit statusChanged();
        return true;
    } catch (const std::exception& error) {
        Fail(error.what());
        return false;
    } catch (...) {
        Fail("Cannot start native preview owner");
        return false;
    }
}

bool NativePreviewWidget::SurfaceValid() const {
    return surface_ && reinterpret_cast<HWND>(internalWinId()) == surface_ &&
        ::IsWindow(surface_);
}

bool NativePreviewWidget::SynchronizeSurface() {
    if (!owner_ || !SurfaceValid()) return false;
    RECT bounds{};
    if (!::GetClientRect(surface_, &bounds)) return false;
    const auto width = static_cast<std::uint32_t>(std::max<LONG>(0, bounds.right - bounds.left));
    const auto height = static_cast<std::uint32_t>(std::max<LONG>(0, bounds.bottom - bounds.top));
    const bool visible = isVisible() && !window()->isMinimized() && ::IsWindowVisible(surface_);
    if (!surfaceSynchronized_ || width != physicalWidth_ || height != physicalHeight_ || visible != physicalVisible_) {
        physicalWidth_ = width;
        physicalHeight_ = height;
        physicalVisible_ = visible;
        surfaceSynchronized_ = true;
        owner_->Resize(width, height, visible);
    }
    return true;
}

void NativePreviewWidget::Fail(const char* error) {
    snapshot_.state = StudioPreviewState::Failed;
    snapshot_.error = error;
    ++snapshot_.sequence;
    emit statusChanged();
}

bool NativePreviewWidget::Load(StudioPreviewRequest request) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (closing_ || !owner_ || owner_->IsStopped() || !SynchronizeSurface()) return false;
    if (retireToken_ > snapshot_.retiredToken ||
        retireToken_ > owner_->Snapshot().retiredToken || !request.identity.requestId ||
        request.identity.requestId <= lastRequest_ || request.identity.projectDirectory.empty() ||
        request.identity.executionId.empty() || request.identity.contentHash.empty() ||
        request.output.empty() || !request.initialView) return false;
    lastRequest_ = request.identity.requestId;
    expected_ = request.identity;
    snapshot_.identity = request.identity;
    snapshot_.state = StudioPreviewState::Loading;
    snapshot_.error.clear();
    snapshot_.submittedFrames = 0;
    snapshot_.diagnosticsJSON.clear();
    snapshot_.capturePending = snapshot_.captureSucceeded = false;
    snapshot_.captureError.clear();
    ++snapshot_.sequence;
    owner_->RequestLoad(std::move(request));
    emit statusChanged();
    return true;
}

void NativePreviewWidget::Retire(std::uint64_t token) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (closing_ || !token || token <= retireToken_) return;
    retireToken_ = token;
    expected_.reset();
    dragging_ = false;
    if (!owner_) {
        // Construction never acquired a renderer, so there is no GPU work to
        // await. Preserve any startup failure rather than claiming a preview
        // recovered merely because this empty boundary can be acknowledged.
        snapshot_.retiredToken = std::max(snapshot_.retiredToken, token);
        ++snapshot_.sequence;
        emit statusChanged();
        return;
    }
    snapshot_.state = StudioPreviewState::Retiring;
    ++snapshot_.sequence;
    owner_->RequestRetire(token);
    emit statusChanged();
}

void NativePreviewWidget::RequestStop() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (closing_) return;
    closing_ = true;
    expected_.reset();
    dragging_ = false;
    if (owner_) {
        owner_->RequestStop();
        snapshot_.state = StudioPreviewState::Retiring;
        ++snapshot_.sequence;
        emit statusChanged();
        poll_.start();
    } else {
        // Keep acknowledgement asynchronous even for an owner never started.
        QTimer::singleShot(0, this, [this] {
            if (stopAcknowledged_) return;
            snapshot_.state = StudioPreviewState::Stopped;
            ++snapshot_.sequence;
            stopAcknowledged_ = true;
            emit statusChanged();
            emit stopped();
        });
    }
}

bool NativePreviewWidget::IsStopped() const { return !owner_ || stopAcknowledged_; }
StudioPreviewStatus NativePreviewWidget::Snapshot() const { return snapshot_; }

void NativePreviewWidget::Poll() {
    if (!owner_) return;
    if (!SurfaceValid()) {
        // Native replacement is outside the supported contract. Do not render
        // to or silently rebind a new handle while the old owner is alive.
        RequestStop();
    }
    if (!closing_ && !SynchronizeSurface()) {
        RequestStop();
        return;
    }
    auto current = owner_->Snapshot();
    if (current.childWindow != reinterpret_cast<std::uintptr_t>(surface_)) return;
    if (current.state == StudioPreviewState::Stopped && owner_->IsStopped()) {
        const auto sequence = snapshot_.sequence + 1;
        snapshot_ = std::move(current);
        snapshot_.sequence = sequence;
        stopAcknowledged_ = true;
        poll_.stop();
        emit statusChanged();
        emit stopped();
        return;
    }
    if (closing_ || current.sequence <= ownerSequence_) return;
    ownerSequence_ = current.sequence;
    if (retireToken_ > current.retiredToken) return;
    if (expected_) {
        if (!SameIdentity(current.identity, *expected_)) return;
    } else if (current.state != StudioPreviewState::Unloaded) {
        return;
    }
    const auto sequence = snapshot_.sequence + 1;
    snapshot_ = std::move(current);
    snapshot_.sequence = sequence;
    emit statusChanged();
}

bool NativePreviewWidget::CanInteract() const {
    return owner_ && !closing_ && expected_ && snapshot_.state == StudioPreviewState::Ready &&
        SurfaceValid() && SameIdentity(snapshot_.identity, *expected_);
}

void NativePreviewWidget::ResetView() { if (CanInteract()) owner_->ResetView(); }

bool NativePreviewWidget::RequestCapture(std::uint64_t id, std::filesystem::path png,
                                        std::filesystem::path metadata) {
    return id && !png.empty() && !metadata.empty() && CanInteract() &&
        owner_->RequestCapture(id, std::move(png), std::move(metadata));
}

bool NativePreviewWidget::event(QEvent* event) {
    const bool live = owner_ && !owner_->IsStopped();
    // Qt does not provide a cancellable native-surface destruction event. These
    // guards fail before unsupported destruction; the shell must retire first.
    if (live && event->type() == QEvent::ParentAboutToChange)
        qFatal("Reparenting a live NativePreviewWidget is unsupported; stop first");
    if (live && event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
        qFatal("Destroying a live NativePreviewWidget native surface is unsupported; stop first");
    if (live && event->type() == QEvent::WinIdChange && !SurfaceValid())
        qFatal("Replacing a live NativePreviewWidget HWND is unsupported; stop first");
    if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)
        dragging_ = false;
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
        UpdateCover();
    if (owner_ && !closing_ && (event->type() == QEvent::Resize ||
        event->type() == QEvent::Show || event->type() == QEvent::Hide)) SynchronizeSurface();
    return handled;
}

void NativePreviewWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && CanInteract()) {
        setFocus(Qt::MouseFocusReason);
        dragging_ = true;
        lastMouse_ = event->position();
        event->accept();
    } else QWidget::mousePressEvent(event);
}

void NativePreviewWidget::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_ && (event->buttons() & Qt::LeftButton) && CanInteract()) {
        // Mouse coordinates and QWidget extent share logical units. Normalize
        // those units for camera input; only swapchain sizing uses native pixels.
        const auto delta = event->position() - lastMouse_;
        owner_->Orbit(static_cast<float>(delta.x() / std::max(1, width())),
                      static_cast<float>(delta.y() / std::max(1, height())));
        lastMouse_ = event->position();
        event->accept();
    } else QWidget::mouseMoveEvent(event);
}

void NativePreviewWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) { dragging_ = false; event->accept(); }
    else QWidget::mouseReleaseEvent(event);
}

void NativePreviewWidget::wheelEvent(QWheelEvent* event) {
    if (CanInteract()) {
        const float steps = event->angleDelta().y() != 0 ? event->angleDelta().y() / 120.f :
            event->pixelDelta().y() / 120.f;
        owner_->Zoom(event->inverted() ? -steps : steps);
        event->accept();
    } else QWidget::wheelEvent(event);
}

void NativePreviewWidget::keyPressEvent(QKeyEvent* event) {
    if (!CanInteract()) { QWidget::keyPressEvent(event); return; }
    switch (event->key()) {
    case Qt::Key_R: case Qt::Key_Home: ResetView(); break;
    case Qt::Key_Left: owner_->Orbit(-.03f, 0); break;
    case Qt::Key_Right: owner_->Orbit(.03f, 0); break;
    case Qt::Key_Up: owner_->Orbit(0, -.03f); break;
    case Qt::Key_Down: owner_->Orbit(0, .03f); break;
    case Qt::Key_Plus: case Qt::Key_Equal: owner_->Zoom(1); break;
    case Qt::Key_Minus: owner_->Zoom(-1); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

QPaintEngine* NativePreviewWidget::paintEngine() const { return nullptr; }

} // namespace MonsterStudioQt
