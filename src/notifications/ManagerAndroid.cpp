// SPDX-FileCopyrightText: 2012 Roland Hieber <rohieb@rohieb.name>
// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

// This is a dummy file because there is currently no working backend for Android notifications.
// This is simply provided so that Android builds will compile.

#include "notifications/Manager.h"

#include <functional>
#include <variant>

#include <mtx/responses/notifications.hpp>

#include "Cache.h"
#include "EventAccessors.h"
#include "Logging.h"
#include "MxcImageProvider.h"
#include "Utils.h"

NotificationsManager::NotificationsManager(QObject *parent)
  : QObject(parent)
{
        nhlog::ui()->warn("Notifications disabled (no Android backend available)");
}

void
NotificationsManager::postNotification(const mtx::responses::Notification &notification,
                                       const QImage &icon)
{
        Q_UNUSED(notification)
        Q_UNUSED(icon)
}

void
NotificationsManager::removeNotification(const QString &roomId, const QString &eventId)
{
        Q_UNUSED(roomId)
        Q_UNUSED(eventId)
}

void
NotificationsManager::actionInvoked(uint id, QString action)
{
        Q_UNUSED(id)
        Q_UNUSED(action)
}

void
NotificationsManager::notificationReplied(uint id, QString reply)
{
        Q_UNUSED(id)
        Q_UNUSED(reply)
}

void
NotificationsManager::notificationClosed(uint id, uint reason)
{
        Q_UNUSED(id)
        Q_UNUSED(reason)
}
