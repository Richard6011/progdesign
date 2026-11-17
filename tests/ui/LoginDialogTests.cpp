#include "ui/LoginDialog.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>

namespace {

QLineEdit *field(nc::LoginDialog &dialog, const char *name) {
    return dialog.findChild<QLineEdit *>(QString::fromLatin1(name));
}

QPushButton *button(nc::LoginDialog &dialog, const char *name) {
    return dialog.findChild<QPushButton *>(QString::fromLatin1(name));
}

int run(const QString &evidenceDirectory) {
    int failures = 0;
    const auto check = [&](const bool condition, const char *message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
        return condition;
    };
    const auto capture = [&](nc::LoginDialog &dialog, const QString &filename) {
        if (evidenceDirectory.isEmpty())
            return;
        dialog.show();
        QCoreApplication::processEvents();
        check(QDir().mkpath(evidenceDirectory) &&
                  dialog.grab().save(QDir(evidenceDirectory).filePath(filename)),
              "login screenshot is saved");
        dialog.hide();
    };
    QTemporaryDir data;
    if (!check(data.isValid(), "temporary account directory is valid"))
        return 1;

    nc::LoginDialog registerDialog(data.path());
    check(registerDialog.registrationMode() == false, "dialog starts in login mode");
    capture(registerDialog, QStringLiteral("ui-login.png"));
    button(registerDialog, "switchMode")->click();
    check(registerDialog.registrationMode(), "switches to registration mode");
    capture(registerDialog, QStringLiteral("ui-register.png"));
    field(registerDialog, "username")->setText("alice");
    field(registerDialog, "password")->setText("plain-pass");
    button(registerDialog, "submit")->click();
    check(!registerDialog.registrationMode(), "successful registration returns to login mode");
    check(registerDialog.result() == QDialog::Rejected && registerDialog.accountName().isEmpty(),
          "registration alone does not authenticate the user");

    nc::LoginDialog loginDialog(data.path());
    field(loginDialog, "username")->setText("alice");
    field(loginDialog, "password")->setText("wrong");
    button(loginDialog, "submit")->click();
    check(loginDialog.result() == 0, "wrong password keeps dialog open");
    QTimer::singleShot(0, &loginDialog, [&] {
        field(loginDialog, "password")->setText("plain-pass");
        button(loginDialog, "submit")->click();
    });
    QTimer::singleShot(2000, &loginDialog, &QDialog::reject);
    const int loginResult = loginDialog.exec();
    check(loginResult == QDialog::Accepted && loginDialog.accountName() == "alice",
          "correct password accepts with account name");

    nc::LoginDialog cancelled(data.path());
    bool rejected = false;
    QObject::connect(&cancelled, &QDialog::rejected, [&] { rejected = true; });
    QTimer::singleShot(0, &cancelled, [&] { button(cancelled, "cancel")->click(); });
    QTimer::singleShot(2000, &cancelled, &QDialog::reject);
    const int cancelResult = cancelled.exec();
    check(rejected && cancelResult == QDialog::Rejected && cancelled.accountName().isEmpty(),
          "exit button rejects login without an authenticated name");
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    const int fontId = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    if (!families.isEmpty())
        application.setFont(QFont(families.front(), 10));
    QCoreApplication::setApplicationName(QStringLiteral("NoteCollisionLoginTests"));
    QCoreApplication::setOrganizationName(QStringLiteral("NoteCollisionTests"));
    return run(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString());
}
