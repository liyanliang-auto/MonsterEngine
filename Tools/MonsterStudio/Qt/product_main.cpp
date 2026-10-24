#include "WorkbenchWindow.h"
#include "WorkbenchController.h"
#include <QApplication>
#include <QIcon>
#include <QDebug>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    const QIcon icon(QStringLiteral(":/monster/monster.ico"));
    if (icon.isNull()) { qCritical("Embedded Monster application icon is missing"); return 1; }
    application.setWindowIcon(icon);
    QCoreApplication::setOrganizationName(QStringLiteral("MonsterEngine"));
    QCoreApplication::setApplicationName(QStringLiteral("MonsterStudioQt"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));
    MonsterStudioQt::WorkbenchWindow window;
    // Reverse destruction order releases the controller before the native window.
    MonsterStudioQt::WorkbenchController controller(window);
    window.show();
    return application.exec();
}
