#include "LoginDialog.h"

#include <QFormLayout>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace nc {

LoginDialog::LoginDialog(QString dataDirectory, QWidget *parent)
    : QDialog(parent), accounts_(std::move(dataDirectory)) {
    setWindowTitle(QStringLiteral("音符碰撞 - 账号登录"));
    setModal(true);
    setMinimumWidth(420);
    setStyleSheet(QStringLiteral(R"(
        QDialog { background: #0b1020; color: #ecf2ff; }
        QLabel { color: #ecf2ff; }
        QLineEdit {
            background: #111a30; color: #ecf2ff;
            border: 1px solid #344566; border-radius: 5px; padding: 8px;
        }
        QPushButton {
            min-height: 40px; background: #1a2745; color: #ecf2ff;
            border: 1px solid #32456e; border-radius: 6px; padding: 8px 16px;
        }
        QPushButton:hover { background: #263961; border-color: #7c94ff; }
        QPushButton#submit { background: #4667d8; border-color: #6987f4; font-weight: bold; }
        QLabel#loginStatus { color: #93a4c6; }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(10);
    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("loginTitle"));
    QFont titleFont = title_->font();
    titleFont.setPixelSize(26);
    titleFont.setBold(true);
    title_->setFont(titleFont);
    layout->addWidget(title_);

    auto *form = new QFormLayout;
    username_ = new QLineEdit(this);
    username_->setObjectName(QStringLiteral("username"));
    username_->setPlaceholderText(QStringLiteral("1-16 个字符"));
    password_ = new QLineEdit(this);
    password_->setObjectName(QStringLiteral("password"));
    password_->setEchoMode(QLineEdit::Password);
    password_->setPlaceholderText(QStringLiteral("至少 1 个字符"));
    form->addRow(QStringLiteral("账号"), username_);
    form->addRow(QStringLiteral("密码"), password_);
    layout->addLayout(form);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("loginStatus"));
    status_->setWordWrap(true);
    status_->setMinimumHeight(48);
    layout->addWidget(status_);

    submit_ = new QPushButton(this);
    submit_->setObjectName(QStringLiteral("submit"));
    submit_->setDefault(true);
    switchMode_ = new QPushButton(this);
    switchMode_->setObjectName(QStringLiteral("switchMode"));
    switchMode_->setAutoDefault(false);
    cancel_ = new QPushButton(QStringLiteral("退出"), this);
    cancel_->setObjectName(QStringLiteral("cancel"));
    cancel_->setAutoDefault(false);
    layout->addWidget(submit_);
    layout->addWidget(switchMode_);
    layout->addWidget(cancel_);

    connect(submit_, &QPushButton::clicked, this, &LoginDialog::submit);
    connect(switchMode_, &QPushButton::clicked, this, &LoginDialog::toggleMode);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    updateTexts();
    username_->setFocus();
}

void LoginDialog::submit() {
    const QString username = username_->text();
    const QString password = password_->text();
    QString error;
    if (registrationMode_) {
        if (!accounts_.registerAccount(username, password, &error)) {
            setStatus(error, true);
            return;
        }
        registrationMode_ = false;
        password_->clear();
        setStatus(QStringLiteral("注册成功，请登录"), false);
        updateTexts();
        password_->setFocus();
        return;
    }

    if (!accounts_.authenticate(username, password, &error)) {
        setStatus(error, true);
        return;
    }
    accountName_ = username.trimmed();
    accept();
}

void LoginDialog::toggleMode() {
    registrationMode_ = !registrationMode_;
    password_->clear();
    setStatus({}, false);
    updateTexts();
}

void LoginDialog::setStatus(const QString &message, const bool error) {
    status_->setText(message);
    status_->setStyleSheet(error ? QStringLiteral("color:#ff809f;")
                                 : QStringLiteral("color:#93a4c6;"));
}

void LoginDialog::updateTexts() {
    title_->setText(registrationMode_ ? QStringLiteral("注册账号") : QStringLiteral("登录账号"));
    submit_->setText(registrationMode_ ? QStringLiteral("注册") : QStringLiteral("登录"));
    switchMode_->setText(registrationMode_ ? QStringLiteral("已有账号，返回登录")
                                           : QStringLiteral("没有账号，注册一个"));
}

} // namespace nc
