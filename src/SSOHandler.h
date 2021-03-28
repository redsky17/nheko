// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <Qt> // for OS defines

#ifndef Q_OS_ANDROID
#include "httplib.h"

#include <QObject>
#include <string>

class SSOHandler : public QObject
{
        Q_OBJECT

public:
        SSOHandler(QObject *parent = nullptr);

        ~SSOHandler();

        std::string url() const;

signals:
        void ssoSuccess(std::string token);
        void ssoFailed();

private:
        httplib::Server svr;
        int port = 0;
};
#endif
