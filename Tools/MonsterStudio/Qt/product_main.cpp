#include "WorkbenchWindow.h"
#include "WorkbenchController.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MonsterEngine"));
    QCoreApplication::setApplicationName(QStringLiteral("MonsterStudioQt"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));
    MonsterStudioQt::WorkbenchWindow window;
    // Reverse destruction order releases the controller before the native window.
    MonsterStudioQt::WorkbenchController controller(window);
    window.show();
    return application.exec();
}
