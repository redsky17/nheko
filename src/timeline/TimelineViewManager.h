// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickView>
#include <QQuickWidget>
#include <QSharedPointer>
#include <QWidget>

#include <mtx/common.hpp>
#include <mtx/responses/messages.hpp>
#include <mtx/responses/sync.hpp>

#include "Cache.h"
#include "CallManager.h"
#include "Logging.h"
#include "TimelineModel.h"
#include "Utils.h"
#include "WebRTCSession.h"
#include "emoji/EmojiModel.h"
#include "emoji/Provider.h"
#include "timeline/CommunitiesModel.h"
#include "timeline/RoomlistModel.h"

class MxcImageProvider;
class BlurhashProvider;
class ColorImageProvider;
class UserSettings;
class ChatPage;
class DeviceVerificationFlow;

class TimelineViewManager : public QObject
{
        Q_OBJECT

        Q_PROPERTY(
          bool isInitialSync MEMBER isInitialSync_ READ isInitialSync NOTIFY initialSyncChanged)
        Q_PROPERTY(
          bool isWindowFocused MEMBER isWindowFocused_ READ isWindowFocused NOTIFY focusChanged)

public:
        TimelineViewManager(CallManager *callManager, ChatPage *parent = nullptr);
        QWidget *getWidget() const { return container; }

        void sync(const mtx::responses::Rooms &rooms);

        MxcImageProvider *imageProvider() { return imgProvider; }
        CallManager *callManager() { return callManager_; }

        void clearAll() { rooms_->clear(); }

        Q_INVOKABLE bool isInitialSync() const { return isInitialSync_; }
        bool isWindowFocused() const { return isWindowFocused_; }
        Q_INVOKABLE void openImageOverlay(QString mxcUrl, QString eventId);
        Q_INVOKABLE QColor userColor(QString id, QColor background);
        Q_INVOKABLE QString escapeEmoji(QString str) const;
        Q_INVOKABLE QString htmlEscape(QString str) const { return str.toHtmlEscaped(); }

        Q_INVOKABLE QString userPresence(QString id) const;
        Q_INVOKABLE QString userStatus(QString id) const;

        Q_INVOKABLE void openRoomMembers(QString room_id);
        Q_INVOKABLE void openRoomSettings(QString room_id);
        Q_INVOKABLE void openInviteUsers(QString roomId);
        Q_INVOKABLE void openGlobalUserProfile(QString userId);

        Q_INVOKABLE void focusMessageInput();
        Q_INVOKABLE void openLeaveRoomDialog(QString roomid) const;
        Q_INVOKABLE void removeVerificationFlow(DeviceVerificationFlow *flow);

        Q_INVOKABLE void fixImageRendering(QQuickTextDocument *t, QQuickItem *i);

        void verifyUser(QString userid);
        void verifyDevice(QString userid, QString deviceid);

signals:
        void activeTimelineChanged(TimelineModel *timeline);
        void initialSyncChanged(bool isInitialSync);
        void replyingEventChanged(QString replyingEvent);
        void replyClosed();
        void newDeviceVerificationRequest(DeviceVerificationFlow *flow);
        void inviteUsers(QString roomId, QStringList users);
        void showRoomList();
        void narrowViewChanged();
        void focusChanged();
        void focusInput();
        void openImageOverlayInternalCb(QString eventId, QImage img);
        void openRoomMembersDialog(MemberList *members);
        void openRoomSettingsDialog(RoomSettings *settings);
        void openInviteUsersDialog(InviteesModel *invitees);
        void openProfile(UserProfile *profile);

public slots:
        void updateReadReceipts(const QString &room_id, const std::vector<QString> &event_ids);
        void receivedSessionKey(const std::string &room_id, const std::string &session_id);
        void initializeRoomlist();
        void chatFocusChanged(bool focused)
        {
                isWindowFocused_ = focused;
                emit focusChanged();
        }

        void showEvent(const QString &room_id, const QString &event_id);
        void focusTimeline();

        void updateColorPalette();
        void queueReply(const QString &roomid,
                        const QString &repliedToEvent,
                        const QString &replyBody);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallInvite &);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallCandidates &);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallAnswer &);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallHangUp &);

        void setVideoCallItem();

        QObject *completerFor(QString completerName, QString roomId = "");
        void forwardMessageToRoom(mtx::events::collections::TimelineEvents *e, QString roomId);

        RoomlistModel *rooms() { return rooms_; }

private slots:
        void openImageOverlayInternal(QString eventId, QImage img);

private:
#ifdef USE_QUICK_VIEW
        QQuickView *view;
#else
        QQuickWidget *view;
#endif
        QWidget *container;

        MxcImageProvider *imgProvider;
        ColorImageProvider *colorImgProvider;
        BlurhashProvider *blurhashProvider;

        CallManager *callManager_ = nullptr;

        bool isInitialSync_   = true;
        bool isWindowFocused_ = false;

        RoomlistModel *rooms_          = nullptr;
        CommunitiesModel *communities_ = nullptr;

        QHash<QString, QColor> userColors;

        QHash<QString, QSharedPointer<DeviceVerificationFlow>> dvList;
};
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationAccept)
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationCancel)
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationDone)
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationKey)
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationMac)
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationReady)
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationRequest)
Q_DECLARE_METATYPE(mtx::events::msg::KeyVerificationStart)
