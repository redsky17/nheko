#pragma once

#include <QHash>
#include <QQuickView>
#include <QQuickWidget>
#include <QSharedPointer>
#include <QWidget>

#include <mtx/common.hpp>
#include <mtx/responses/messages.hpp>
#include <mtx/responses/sync.hpp>

#include "Cache.h"
#include "CallManager.h"
#include "JdenticonProvider.h"
#include "Logging.h"
#include "TimelineModel.h"
#include "Utils.h"
#include "WebRTCSession.h"
#include "emoji/EmojiModel.h"
#include "emoji/Provider.h"

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
          TimelineModel *timeline MEMBER timeline_ READ activeTimeline NOTIFY activeTimelineChanged)
        Q_PROPERTY(
          bool isInitialSync MEMBER isInitialSync_ READ isInitialSync NOTIFY initialSyncChanged)
        Q_PROPERTY(
          bool isNarrowView MEMBER isNarrowView_ READ isNarrowView NOTIFY narrowViewChanged)
        Q_PROPERTY(
          bool isWindowFocused MEMBER isWindowFocused_ READ isWindowFocused NOTIFY focusChanged)

public:
        TimelineViewManager(CallManager *callManager, ChatPage *parent = nullptr);
        QWidget *getWidget() const { return container; }

        void sync(const mtx::responses::Rooms &rooms);
        void addRoom(const QString &room_id);

        void clearAll()
        {
                timeline_ = nullptr;
                emit activeTimelineChanged(nullptr);
                models.clear();
        }

        Q_INVOKABLE TimelineModel *activeTimeline() const { return timeline_; }
        Q_INVOKABLE bool isInitialSync() const { return isInitialSync_; }
        bool isNarrowView() const { return isNarrowView_; }
        bool isWindowFocused() const { return isWindowFocused_; }
        Q_INVOKABLE void openImageOverlay(QString mxcUrl, QString eventId) const;
        Q_INVOKABLE QColor userColor(QString id, QColor background);
        Q_INVOKABLE QString escapeEmoji(QString str) const;

        Q_INVOKABLE QString userPresence(QString id) const;
        Q_INVOKABLE QString userStatus(QString id) const;

        Q_INVOKABLE void openLink(QString link) const;

        Q_INVOKABLE void focusMessageInput();
        Q_INVOKABLE void openInviteUsersDialog();
        Q_INVOKABLE void openMemberListDialog() const;
        Q_INVOKABLE void openLeaveRoomDialog() const;
        Q_INVOKABLE void removeVerificationFlow(DeviceVerificationFlow *flow);

        void verifyUser(QString userid);
        void verifyDevice(QString userid, QString deviceid);

signals:
        void clearRoomMessageCount(QString roomid);
        void updateRoomsLastMessage(QString roomid, const DescInfo &info);
        void activeTimelineChanged(TimelineModel *timeline);
        void initialSyncChanged(bool isInitialSync);
        void replyingEventChanged(QString replyingEvent);
        void replyClosed();
        void newDeviceVerificationRequest(DeviceVerificationFlow *flow);
        void inviteUsers(QStringList users);
        void showRoomList();
        void narrowViewChanged();
        void focusChanged();
        void focusInput();

public slots:
        void updateReadReceipts(const QString &room_id, const std::vector<QString> &event_ids);
        void receivedSessionKey(const std::string &room_id, const std::string &session_id);
        void initWithMessages(const std::vector<QString> &roomIds);
        void chatFocusChanged(bool focused)
        {
                isWindowFocused_ = focused;
                emit focusChanged();
        }

        void setHistoryView(const QString &room_id);
        TimelineModel *getHistoryView(const QString &room_id)
        {
                auto room = models.find(room_id);
                if (room != models.end())
                        return room.value().data();
                else
                        return nullptr;
        }

        void updateColorPalette();
        void queueReply(const QString &roomid,
                        const QString &repliedToEvent,
                        const QString &replyBody);
        void queueReactionMessage(const QString &reactedEvent, const QString &reactionKey);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallInvite &);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallCandidates &);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallAnswer &);
        void queueCallMessage(const QString &roomid, const mtx::events::msg::CallHangUp &);

        void updateEncryptedDescriptions();
        void setVideoCallItem();

        void enableBackButton()
        {
                if (isNarrowView_)
                        return;
                isNarrowView_ = true;
                emit narrowViewChanged();
        }
        void disableBackButton()
        {
                if (!isNarrowView_)
                        return;
                isNarrowView_ = false;
                emit narrowViewChanged();
        }

        void backToRooms() { emit showRoomList(); }

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
        JdenticonProvider *jdenticonProvider;

        QHash<QString, QSharedPointer<TimelineModel>> models;
        TimelineModel *timeline_  = nullptr;
        CallManager *callManager_ = nullptr;

        bool isInitialSync_   = true;
        bool isNarrowView_    = false;
        bool isWindowFocused_ = false;

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
