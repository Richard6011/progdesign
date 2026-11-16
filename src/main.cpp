#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("音符碰撞"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QApplication::setOrganizationName(QStringLiteral("NoteCollision"));

    nc::MainWindow window;
    window.show();
    return application.exec();
}
