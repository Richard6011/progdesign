#pragma once

#include "storage/AccountRepository.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace nc {

class LoginDialog : public QDialog {
    Q_OBJECT

  public:
    explicit LoginDialog(QString dataDirectory = {}, QWidget *parent = nullptr);

    QString accountName() const {
        return accountName_;
    }
    bool registrationMode() const {
        return registrationMode_;
    }

  private:
    void submit();
    void toggleMode();
    void setStatus(const QString &message, bool error);
    void updateTexts();

    AccountRepository accounts_;
    QString accountName_;
    bool registrationMode_ = false;
    QLabel *title_ = nullptr;
    QLabel *status_ = nullptr;
    QLineEdit *username_ = nullptr;
    QLineEdit *password_ = nullptr;
    QPushButton *submit_ = nullptr;
    QPushButton *switchMode_ = nullptr;
    QPushButton *cancel_ = nullptr;
};

} // namespace nc
