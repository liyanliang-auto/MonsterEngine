#pragma once

#include "NativePreviewWidget.h"
#include <QByteArray>
#include <QMainWindow>
#include <QTimer>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#ifdef MONSTER_STUDIO_QT_TESTING
#include <functional>
#endif

class QLabel;
class QLineEdit;
class QPushButton;
class QPlainTextEdit;

namespace MonsterStudioQt {
/** Stage-1 host only. Owns one serial service worker and a fixed native viewport. */
class HostValidationWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit HostValidationWindow(QWidget* parent = nullptr);
    ~HostValidationWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
#ifdef MONSTER_STUDIO_QT_TESTING
    // Compiled only by the verification target. Install under mutex_ before
    // dispatch; the worker copies under that lock and invokes outside it.
    friend struct HostValidationWindowTestAccess;
    std::function<void()> afterInspectionLeaseAcquiredForTest_;
#endif
    enum class Operation { Open, Release, Export, Stop };
    struct Command {
        Operation operation{Operation::Release};
        std::uint64_t generation{0};
        std::filesystem::path path;
        QByteArray diagnostic;
    };
    struct Completion {
        Operation operation{Operation::Release};
        std::uint64_t generation{0};
        StudioPreviewRequest request;
        std::filesystem::path path;
        std::string error;
    };
    void WorkerLoop();
    void Enqueue(Command command);
    void Poll();
    void ChooseProject();
    void BeginOpen();
    void BeginRetire(std::optional<Command> next);
    void ExportDiagnostics();
    void UpdateControls();
    void SetError(const QString& message);
    void RefreshPreview();
    NativePreviewWidget* preview_{};
    QLineEdit* project_{};
    QPushButton *browse_{}, *load_{}, *retire_{}, *reset_{}, *export_{};
    QLabel *status_{}, *activity_{};
    QPlainTextEdit* details_{};
    QTimer poll_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Command> commands_;
    std::deque<Completion> completions_;
    std::thread worker_;
    bool workerDone_{false}, busy_{false}, closing_{false}, closeReady_{false};
    bool stopQueued_{false}, ownerStarted_{false};
    std::uint64_t generation_{0}, requestId_{0}, retireToken_{0}, pendingRetire_{0};
    std::uint64_t captureId_{0}, pendingCapture_{0}, heartbeat_{0};
    std::optional<Command> afterRetire_;
    QString failure_;
};
} // namespace MonsterStudioQt
