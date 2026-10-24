#include "HostValidationWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MonsterEngine"));
    QCoreApplication::setApplicationName(QStringLiteral("MonsterStudioQtHost"));
    QCoreApplication::setApplicationVersion(QStringLiteral("stage-1"));
    MonsterStudioQt::HostValidationWindow window;
    window.show();
    return application.exec();
}
