#include "HostValidationWindow.h"
#include <QApplication>
#include <QIcon>
#include <QDebug>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    const QIcon icon(QStringLiteral(":/monster/monster.ico"));
    if (icon.isNull()) { qCritical("Embedded Monster application icon is missing"); return 1; }
    application.setWindowIcon(icon);
    QCoreApplication::setOrganizationName(QStringLiteral("MonsterEngine"));
    QCoreApplication::setApplicationName(QStringLiteral("MonsterStudioQtHost"));
    QCoreApplication::setApplicationVersion(QStringLiteral("stage-1"));
    MonsterStudioQt::HostValidationWindow window;
    window.show();
    return application.exec();
}
