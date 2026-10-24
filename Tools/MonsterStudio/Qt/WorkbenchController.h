#pragma once
#include <QObject>
#include <memory>

namespace MonsterStudioQt {
class WorkbenchWindow;
/** Owns the serial project worker and asynchronous native-owner lifecycle.
 * Automatic viewing requires the current published execution; explicit history
 * inspection stays independent. Actual event progress and persisted timing are
 * presented without estimating an overall completion percentage.
 */
class WorkbenchController final : public QObject {
    Q_OBJECT
public:
    explicit WorkbenchController(WorkbenchWindow& window, QObject* parent = nullptr);
    ~WorkbenchController() override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
