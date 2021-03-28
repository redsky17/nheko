// SPDX-FileCopyrightText: 2017 Konstantinos Sideris <siderisk@auth.gr>
// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

class FlatButton;
class LoadingIndicator;
class OverlayModal;
class RaisedButton;
class TextField;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;

namespace mtx {
namespace responses {
struct Login;
}
}

class LoginPage : public QWidget
{
        Q_OBJECT

public:
        enum class LoginMethod
        {
                Password,
                SSO,
        };

        LoginPage(QWidget *parent = nullptr);

        void reset();

signals:
        void backButtonClicked();
        void loggingIn();
        void errorOccurred();

        //! Used to trigger the corresponding slot outside of the main thread.
        void versionErrorCb(const QString &err);
        void versionOkCb(bool passwordSupported, bool ssoSupported);

        void loginOk(const mtx::responses::Login &res);
        void showErrorMessage(QLabel *label, const QString &msg);

protected:
        void paintEvent(QPaintEvent *event) override;

public slots:
        // Displays errors produced during the login.
        void showError(const QString &msg);
        void showError(QLabel *label, const QString &msg);

private slots:
        // Callback for the back button.
        void onBackButtonClicked();

        // Callback for the login button.
        void onLoginButtonClicked(LoginMethod loginMethod);

        // Callback for probing the server found in the mxid
        void onMatrixIdEntered();

        // Callback for probing the manually entered server
        void onServerAddressEntered();

        // Callback for errors produced during server probing
        void versionError(const QString &error_message);
        // Callback for successful server probing
        void versionOk(bool passwordSupported, bool ssoSupported);

private:
        void checkHomeserverVersion();
        std::string initialDeviceName()
        {
#if defined(Q_OS_MAC)
                return "Nheko on macOS";
// Android is before Linux because it is also detected as Linux
#elif defined(Q_OS_ANDROID)
                return "Nheko on Android";
#elif defined(Q_OS_LINUX)
                return "Nheko on Linux";
#elif defined(Q_OS_WIN)
                return "Nheko on Windows";
#elif defined(Q_OS_FREEBSD)
                return "Nheko on FreeBSD";
#else
                return "Nheko";
#endif
        }

        QVBoxLayout *top_layout_;

        QHBoxLayout *top_bar_layout_;
        QHBoxLayout *logo_layout_;
        QHBoxLayout *button_layout_;

        QLabel *logo_;
        QLabel *error_label_;
        QLabel *error_matrixid_label_;

        QHBoxLayout *serverLayout_;
        QHBoxLayout *matrixidLayout_;
        LoadingIndicator *spinner_;
        QLabel *errorIcon_;
        QString inferredServerAddress_;

        FlatButton *back_button_;
        RaisedButton *login_button_, *sso_login_button_;

        QWidget *form_widget_;
        QHBoxLayout *form_wrapper_;
        QVBoxLayout *form_layout_;

        TextField *matrixid_input_;
        TextField *password_input_;
        TextField *deviceName_;
        TextField *serverInput_;
        bool passwordSupported = true;
        bool ssoSupported      = false;
};
