// SPDX-FileCopyrightText: 2012 Roland Hieber <rohieb@rohieb.name>
// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "notifications/Manager.h"

#include <functional>
#include <variant>

#include <mtx/responses/notifications.hpp>

#include "Cache.h"
#include "EventAccessors.h"
#include "MxcImageProvider.h"
#include "Utils.h"

NotificationsManager::NotificationsManager(QObject *parent)
  : QObject(parent)
{
}

void
NotificationsManager::postNotification(const mtx::responses::Notification &notification,
                                       const QImage &icon)
{
}

void
NotificationsManager::removeNotification(const QString &roomId, const QString &eventId)
{
}

void
NotificationsManager::actionInvoked(uint id, QString action)
{
}

void
NotificationsManager::notificationReplied(uint id, QString reply)
{
}

void
NotificationsManager::notificationClosed(uint id, uint reason)
{
}

