#pragma once

#include <QPointF>
#include <QTimer>
#include <QWidget>
#include "../StudioPreviewHost.h"
#ifdef MONSTER_STUDIO_QT_TESTING
#include <functional>
#endif
class QLabel;

namespace MonsterStudioQt {

/** Native Qt surface borrowing the existing single Vulkan owner.
 * All methods and snapshots belong to the GUI thread. Once StartOwner succeeds,
 * this widget and its ancestors must retain their native HWNDs and parenting.
 * Hide, resize and DPI changes are supported; reparent/setWindowFlags/destroy
 * are not supported until RequestStop has been acknowledged by stopped().
 * The shell must ignore close events until that acknowledgement. */
class NativePreviewWidget final : public QWidget {
    Q_OBJECT
public:
    explicit NativePreviewWidget(QWidget* parent = nullptr);
    ~NativePreviewWidget() override;

    /** Start exactly one owner for this native surface; no scene is loaded. */
    bool StartOwner();
    /** Submit a worker-validated request with a never-reused requestId. */
    bool Load(StudioPreviewRequest request);
    /** Observe Snapshot().retiredToken before releasing a lease. An absent owner
     * acknowledges the empty boundary immediately; a live owner retires asynchronously. */
    void Retire(std::uint64_t token);
    /** Permanently stop this owner asynchronously; no restart is supported. */
    void RequestStop();
    /** True only before start or after actual owner acknowledgement. */
    bool IsStopped() const;
    /** Copy of the accepted current lineage, with no renderer references. */
    StudioPreviewStatus Snapshot() const;
    /** Restore the validated initial scene camera. */
    void ResetView();
    /** Request existing owner diagnostics to explicit local evidence files. */
    bool RequestCapture(std::uint64_t id, std::filesystem::path png,
                        std::filesystem::path metadata);

signals:
    void statusChanged();
    void stopped();

protected:
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    QPaintEngine* paintEngine() const override;

private:
#ifdef MONSTER_STUDIO_QT_TESTING
    // Verification reads the real owner's thread-safe copy, rather than the
    // GUI's optimistic Loading snapshot, before exercising in-flight close.
    friend struct HostValidationWindowTestAccess;
    friend struct NativePreviewWidgetTestAccess;
    // Empty in normal verification runs; unavailable in shipping builds.
    static std::function<std::unique_ptr<StudioPreviewHost>(HWND)> ownerFactoryForTesting_;
#endif
    void Poll();
    bool SurfaceValid() const;
    bool SynchronizeSurface();
    void Fail(const char* error);
    bool CanInteract() const;
    void UpdateCover();

    std::unique_ptr<StudioPreviewHost> owner_;
    QTimer poll_;
    HWND surface_{nullptr};
    StudioPreviewStatus snapshot_;
    std::optional<StudioPreviewIdentity> expected_;
    std::uint64_t lastRequest_{0}, retireToken_{0}, ownerSequence_{0};
    std::uint32_t physicalWidth_{0}, physicalHeight_{0};
    bool physicalVisible_{false}, surfaceSynchronized_{false};
    bool closing_{false}, stopAcknowledged_{false}, dragging_{false};
    QPointF lastMouse_;
    // A real native child covers the native Vulkan surface while non-ready.
    // The render HWND itself remains visible and sized, so Loading can submit.
    QWidget* cover_{};
    QLabel* coverText_{};
};

} // namespace MonsterStudioQt
