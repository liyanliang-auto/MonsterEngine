#pragma once
#include <QMainWindow>
#include <QList>
#include <QString>
#include <QByteArray>

class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;
class QTreeWidget;
class QStackedWidget;
class QProgressBar;
class QPlainTextEdit;
class QSplitter;
class QCloseEvent;
class QResizeEvent;

namespace MonsterStudioQt {
class NativePreviewWidget;
/** Editable selection only; selecting a file never creates project data. */
struct WorkbenchNewTask { QString video,name,root,advancedConfig; int profile{1}; };
/** Cached navigation observation, identified by directory rather than label. */
struct WorkbenchProjectRow { QString path,name,observedState; };
/** Execution metadata; viewable is supplied by the service/controller. */
struct WorkbenchExecutionRow { QString executionId,state,profile,completedAt,activeTime; bool viewable{false}; };
/** Immutable presentation of actual backend observations; unknown training is -1. */
struct WorkbenchState {
    QString projectName,projectPath,status,stage,detail,activeTime,completedAt,trainingText,environment;
    bool busy{false},pauseAvailable{false},resumeAvailable{false},controlPending{false},closing{false};
    int trainingPercent{-1};
};
/** View and input intents only. Project/worker ownership belongs to the controller. */
class WorkbenchWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit WorkbenchWindow(QWidget* parent=nullptr);
    WorkbenchNewTask NewTask() const;
    void SetSuggestedTask(const WorkbenchNewTask&);
    void ApplyState(const WorkbenchState&);
    void SetRecentProjects(const QList<WorkbenchProjectRow>&);
    void SetExecutions(const QList<WorkbenchExecutionRow>&);
    void ApplyTheme(int preference);
    void SetProjectRoot(const QString&);
    NativePreviewWidget* Preview() const;
    void ShowWorkbench();
    void ShowResult(const QString& projectName,const QString& detail);
    /** Selected verified execution timing, independent of the latest task. */
    void SetResultTiming(const QString& completedAt,const QString& activeTime);
    void ShowSettings();
    void ShowError(const QString& title,const QString& message);
    void PermitClose();
signals:
    void videoSelected(const QString& path);
    void startRequested();
    void openProjectRequested(const QString& path);
    void executionRequested(const QString& executionId);
    void pauseRequested();
    void resumeRequested();
    void forceStopRequested();
    void backRequested();
    void retryPreviewRequested();
    void themeRequested(int preference);
    void rootRequested(const QString& root);
    void provisionRuntimeRequested(const QString& config);
    void removeRecentRequested(const QString& path);
    void relocateRecentRequested(const QString& oldPath,const QString& newPath);
    void refreshRequested();
    void closeRequested();
protected:
    void closeEvent(QCloseEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool eventFilter(QObject*,QEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    void FitToAvailableGeometry(const QRect& available);
#ifdef MONSTER_STUDIO_QT_TESTING
    friend struct WorkbenchWindowTestAccess;
#endif
    void ChooseVideo();
    void SelectVideo(const QString&);
    void UpdateTarget();
    void UpdateControls();
    void ToggleFullscreen();
    void ExitFullscreen();
    void RefreshPalette();
    bool AcceptVideo(const QString&) const;
    NativePreviewWidget* preview_{};
    QStackedWidget* pages_{};
    QWidget *nav_{},*newForm_{},*resultPage_{},*advancedGroup_{};
    QSplitter* workColumns_{};
    QLineEdit *video_{},*name_{},*root_{},*settingsRoot_{},*advanced_{};
    QComboBox *quality_{},*theme_{};
    QLabel *target_{},*currentName_{},*stateLabel_{},*stage_{},*activeTime_{},*completedAt_{},*trainingText_{},*environment_{},*settingsEnvironment_{},*resultName_{},*resultTimes_{},*historyEmpty_{},*importHint_{};
    QPlainTextEdit *details_{},*resultDetails_{};
    QProgressBar *training_{},*stageProgress_{};
    QTreeWidget* projects_{};
    QPushButton *choose_{},*open_{},*start_{},*pause_{},*resume_{},*force_{},*advancedChoose_{},*rootChoose_{},*refresh_{},*remove_{},*relocate_{},*runtime_{},*settingsRootChoose_{},*reset_{},*retry_{},*fullscreenButton_{};
    WorkbenchState state_;
    QList<WorkbenchProjectRow> recent_;
    QList<WorkbenchExecutionRow> executions_;
    QString executionsProjectPath_;
    int themePreference_{0};
    bool closePermitted_{false},fullscreen_{false};
    QByteArray placement_;
    qintptr windowStyle_{0},windowExStyle_{0};
};
}
