// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <Qt> // for platform definitions

#ifndef Q_OS_ANDROID // sso is currently not supported for Android
#include "SSOHandler.h"

#include <QTimer>

#include <thread>

#include "Logging.h"

SSOHandler::SSOHandler(QObject *)
{
        QTimer::singleShot(120000, this, &SSOHandler::ssoFailed);

        using namespace httplib;

        svr.set_logger([](const Request &req, const Response &res) {
                nhlog::net()->info("req: {}, res: {}", req.path, res.status);
        });

        svr.Get("/sso", [this](const Request &req, Response &res) {
                if (req.has_param("loginToken")) {
                        auto val = req.get_param_value("loginToken");
                        res.set_content("SSO success", "text/plain");
                        emit ssoSuccess(val);
                } else {
                        res.set_content("Missing loginToken for SSO login!", "text/plain");
                        emit ssoFailed();
                }
        });

        std::thread t([this]() {
                this->port = svr.bind_to_any_port("localhost");
                svr.listen_after_bind();
        });
        t.detach();

        while (!svr.is_running()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
}

SSOHandler::~SSOHandler()
{
        svr.stop();
        while (svr.is_running()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
}

std::string
SSOHandler::url() const
{
        return "http://localhost:" + std::to_string(port) + "/sso";
}

#endif
