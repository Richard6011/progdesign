#include "ui/MainWindow.h"
#include "ui/LoginDialog.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("音符碰撞"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QApplication::setOrganizationName(QStringLiteral("NoteCollision"));

    nc::LoginDialog login;
    if (login.exec() != QDialog::Accepted)
        return 0;

    nc::MainWindow window(nullptr, {}, login.accountName());
    window.show();
    return application.exec();
}
