#include "TimelineViewManager.h"

#include <QDesktopServices>
#include <QDropEvent>
#include <QMetaType>
#include <QPalette>
#include <QQmlContext>
#include <QQmlEngine>
#include <QString>

#include "BlurhashProvider.h"
#include "ChatPage.h"
#include "ColorImageProvider.h"
#include "DelegateChooser.h"
#include "DeviceVerificationFlow.h"
#include "Logging.h"
#include "MainWindow.h"
#include "MatrixClient.h"
#include "MxcImageProvider.h"
#include "UserSettingsPage.h"
#include "dialogs/ImageOverlay.h"
#include "emoji/EmojiModel.h"
#include "emoji/Provider.h"
#include "ui/NhekoCursorShape.h"
#include "ui/NhekoDropArea.h"

#include <iostream> //only for debugging

Q_DECLARE_METATYPE(mtx::events::collections::TimelineEvents)
Q_DECLARE_METATYPE(std::vector<DeviceInfo>)

namespace msgs = mtx::events::msg;

void
TimelineViewManager::updateEncryptedDescriptions()
{
        auto decrypt = ChatPage::instance()->userSettings()->decryptSidebar();
        QHash<QString, QSharedPointer<TimelineModel>>::iterator i;
        for (i = models.begin(); i != models.end(); ++i) {
                auto ptr = i.value();

                if (!ptr.isNull()) {
                        ptr->setDecryptDescription(decrypt);
                        ptr->updateLastMessage();
                }
        }
}

void
TimelineViewManager::updateColorPalette()
{
        userColors.clear();

        if (ChatPage::instance()->userSettings()->theme() == "light") {
                view->rootContext()->setContextProperty("currentActivePalette", QPalette());
                view->rootContext()->setContextProperty("currentInactivePalette", QPalette());
        } else if (ChatPage::instance()->userSettings()->theme() == "dark") {
                view->rootContext()->setContextProperty("currentActivePalette", QPalette());
                view->rootContext()->setContextProperty("currentInactivePalette", QPalette());
        } else {
                view->rootContext()->setContextProperty("currentActivePalette", QPalette());
                view->rootContext()->setContextProperty("currentInactivePalette", nullptr);
        }
}

QColor
TimelineViewManager::userColor(QString id, QColor background)
{
        if (!userColors.contains(id))
                userColors.insert(
                  id, QColor(utils::generateContrastingHexColor(id, background.name())));
        return userColors.value(id);
}

QString
TimelineViewManager::userPresence(QString id) const
{
        if (id.isEmpty())
                return "";
        else
                return QString::fromStdString(
                  mtx::presence::to_string(cache::presenceState(id.toStdString())));
}

QString
TimelineViewManager::userStatus(QString id) const
{
        return QString::fromStdString(cache::statusMessage(id.toStdString()));
}

TimelineViewManager::TimelineViewManager(CallManager *callManager, ChatPage *parent)
  : imgProvider(new MxcImageProvider())
  , colorImgProvider(new ColorImageProvider())
  , blurhashProvider(new BlurhashProvider())
  , jdenticonProvider(new JdenticonProvider())
  , callManager_(callManager)
{
        qRegisterMetaType<mtx::events::msg::KeyVerificationAccept>();
        qRegisterMetaType<mtx::events::msg::KeyVerificationCancel>();
        qRegisterMetaType<mtx::events::msg::KeyVerificationDone>();
        qRegisterMetaType<mtx::events::msg::KeyVerificationKey>();
        qRegisterMetaType<mtx::events::msg::KeyVerificationMac>();
        qRegisterMetaType<mtx::events::msg::KeyVerificationReady>();
        qRegisterMetaType<mtx::events::msg::KeyVerificationRequest>();
        qRegisterMetaType<mtx::events::msg::KeyVerificationStart>();

        qmlRegisterUncreatableMetaObject(qml_mtx_events::staticMetaObject,
                                         "im.nheko",
                                         1,
                                         0,
                                         "MtxEvent",
                                         "Can't instantiate enum!");
        qmlRegisterUncreatableMetaObject(verification::staticMetaObject,
                                         "im.nheko",
                                         1,
                                         0,
                                         "VerificationStatus",
                                         "Can't instantiate enum!");

        qmlRegisterType<DelegateChoice>("im.nheko", 1, 0, "DelegateChoice");
        qmlRegisterType<DelegateChooser>("im.nheko", 1, 0, "DelegateChooser");
        qmlRegisterType<NhekoDropArea>("im.nheko", 1, 0, "NhekoDropArea");
        qmlRegisterType<NhekoCursorShape>("im.nheko", 1, 0, "CursorShape");
        qmlRegisterUncreatableType<DeviceVerificationFlow>(
          "im.nheko", 1, 0, "DeviceVerificationFlow", "Can't create verification flow from QML!");
        qmlRegisterUncreatableType<UserProfile>(
          "im.nheko",
          1,
          0,
          "UserProfileModel",
          "UserProfile needs to be instantiated on the C++ side");
        qmlRegisterUncreatableType<RoomSettings>(
          "im.nheko",
          1,
          0,
          "RoomSettingsModel",
          "Room Settings needs to be instantiated on the C++ side");

        static auto self = this;
        qmlRegisterSingletonType<MainWindow>(
          "im.nheko", 1, 0, "MainWindow", [](QQmlEngine *, QJSEngine *) -> QObject * {
                  return MainWindow::instance();
          });
        qmlRegisterSingletonType<TimelineViewManager>(
          "im.nheko", 1, 0, "TimelineManager", [](QQmlEngine *, QJSEngine *) -> QObject * {
                  return self;
          });
        qmlRegisterSingletonType<UserSettings>(
          "im.nheko", 1, 0, "Settings", [](QQmlEngine *, QJSEngine *) -> QObject * {
                  return ChatPage::instance()->userSettings().data();
          });
        qmlRegisterSingletonType<CallManager>(
          "im.nheko", 1, 0, "CallManager", [](QQmlEngine *, QJSEngine *) -> QObject * {
                  return ChatPage::instance()->callManager();
          });

        qRegisterMetaType<mtx::events::collections::TimelineEvents>();
        qRegisterMetaType<std::vector<DeviceInfo>>();

        qmlRegisterType<emoji::EmojiModel>("im.nheko.EmojiModel", 1, 0, "EmojiModel");
        qmlRegisterType<emoji::EmojiProxyModel>("im.nheko.EmojiModel", 1, 0, "EmojiProxyModel");
        qmlRegisterUncreatableType<QAbstractItemModel>(
          "im.nheko.EmojiModel", 1, 0, "QAbstractItemModel", "Used by proxy models");
        qmlRegisterUncreatableType<emoji::Emoji>(
          "im.nheko.EmojiModel", 1, 0, "Emoji", "Used by emoji models");
        qmlRegisterUncreatableMetaObject(emoji::staticMetaObject,
                                         "im.nheko.EmojiModel",
                                         1,
                                         0,
                                         "EmojiCategory",
                                         "Error: Only enums");

#ifdef USE_QUICK_VIEW
        view      = new QQuickView();
        container = QWidget::createWindowContainer(view, parent);
#else
        view      = new QQuickWidget(parent);
        container = view;
        view->setResizeMode(QQuickWidget::SizeRootObjectToView);
        container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        connect(view, &QQuickWidget::statusChanged, this, [](QQuickWidget::Status status) {
                nhlog::ui()->debug("Status changed to {}", status);
        });
#endif
        container->setMinimumSize(200, 200);
        updateColorPalette();
        view->engine()->addImageProvider("MxcImage", imgProvider);
        view->engine()->addImageProvider("colorimage", colorImgProvider);
        view->engine()->addImageProvider("blurhash", blurhashProvider);
        if (JdenticonProvider::isAvailable())
                view->engine()->addImageProvider("jdenticon", jdenticonProvider);
        view->setSource(QUrl("qrc:///qml/TimelineView.qml"));

        connect(parent, &ChatPage::themeChanged, this, &TimelineViewManager::updateColorPalette);
        connect(parent,
                &ChatPage::decryptSidebarChanged,
                this,
                &TimelineViewManager::updateEncryptedDescriptions);
        connect(
          dynamic_cast<ChatPage *>(parent),
          &ChatPage::receivedRoomDeviceVerificationRequest,
          this,
          [this](const mtx::events::RoomEvent<mtx::events::msg::KeyVerificationRequest> &message,
                 TimelineModel *model) {
                  auto event_id = QString::fromStdString(message.event_id);
                  if (!this->dvList.contains(event_id)) {
                          if (auto flow = DeviceVerificationFlow::NewInRoomVerification(
                                this,
                                model,
                                message.content,
                                QString::fromStdString(message.sender),
                                event_id)) {
                                  dvList[event_id] = flow;
                                  emit newDeviceVerificationRequest(flow.data());
                          }
                  }
          });
        connect(dynamic_cast<ChatPage *>(parent),
                &ChatPage::receivedDeviceVerificationRequest,
                this,
                [this](const mtx::events::msg::KeyVerificationRequest &msg, std::string sender) {
                        if (!msg.transaction_id)
                                return;

                        auto txnid = QString::fromStdString(msg.transaction_id.value());
                        if (!this->dvList.contains(txnid)) {
                                if (auto flow = DeviceVerificationFlow::NewToDeviceVerification(
                                      this, msg, QString::fromStdString(sender), txnid)) {
                                        dvList[txnid] = flow;
                                        emit newDeviceVerificationRequest(flow.data());
                                }
                        }
                });
        connect(dynamic_cast<ChatPage *>(parent),
                &ChatPage::receivedDeviceVerificationStart,
                this,
                [this](const mtx::events::msg::KeyVerificationStart &msg, std::string sender) {
                        if (!msg.transaction_id)
                                return;

                        auto txnid = QString::fromStdString(msg.transaction_id.value());
                        if (!this->dvList.contains(txnid)) {
                                if (auto flow = DeviceVerificationFlow::NewToDeviceVerification(
                                      this, msg, QString::fromStdString(sender), txnid)) {
                                        dvList[txnid] = flow;
                                        emit newDeviceVerificationRequest(flow.data());
                                }
                        }
                });
        connect(parent, &ChatPage::loggedOut, this, [this]() {
                isInitialSync_ = true;
                emit initialSyncChanged(true);
        });
}

void
TimelineViewManager::setVideoCallItem()
{
        WebRTCSession::instance().setVideoItem(
          view->rootObject()->findChild<QQuickItem *>("videoCallItem"));
}

void
TimelineViewManager::sync(const mtx::responses::Rooms &rooms)
{
        for (const auto &[room_id, room] : rooms.join) {
                // addRoom will only add the room, if it doesn't exist
                addRoom(QString::fromStdString(room_id));
                const auto &room_model = models.value(QString::fromStdString(room_id));
                if (!isInitialSync_)
                        connect(room_model.data(),
                                &TimelineModel::newCallEvent,
                                callManager_,
                                &CallManager::syncEvent);
                room_model->syncState(room.state);
                room_model->addEvents(room.timeline);
                if (!isInitialSync_)
                        disconnect(room_model.data(),
                                   &TimelineModel::newCallEvent,
                                   callManager_,
                                   &CallManager::syncEvent);

                if (ChatPage::instance()->userSettings()->typingNotifications()) {
                        for (const auto &ev : room.ephemeral.events) {
                                if (auto t = std::get_if<
                                      mtx::events::EphemeralEvent<mtx::events::ephemeral::Typing>>(
                                      &ev)) {
                                        std::vector<QString> typing;
                                        typing.reserve(t->content.user_ids.size());
                                        for (const auto &user : t->content.user_ids) {
                                                if (user != http::client()->user_id().to_string())
                                                        typing.push_back(
                                                          QString::fromStdString(user));
                                        }
                                        room_model->updateTypingUsers(typing);
                                }
                        }
                }
        }

        this->isInitialSync_ = false;
        emit initialSyncChanged(false);
}

void
TimelineViewManager::addRoom(const QString &room_id)
{
        if (!models.contains(room_id)) {
                QSharedPointer<TimelineModel> newRoom(new TimelineModel(this, room_id));
                newRoom->setDecryptDescription(
                  ChatPage::instance()->userSettings()->decryptSidebar());

                connect(newRoom.data(),
                        &TimelineModel::newEncryptedImage,
                        imgProvider,
                        &MxcImageProvider::addEncryptionInfo);
                models.insert(room_id, std::move(newRoom));
        }
}

void
TimelineViewManager::setHistoryView(const QString &room_id)
{
        nhlog::ui()->info("Trying to activate room {}", room_id.toStdString());

        auto room = models.find(room_id);
        if (room != models.end()) {
                timeline_ = room.value().data();
                emit activeTimelineChanged(timeline_);
                container->setFocus();
                nhlog::ui()->info("Activated room {}", room_id.toStdString());
        }
}

QString
TimelineViewManager::escapeEmoji(QString str) const
{
        return utils::replaceEmoji(str);
}

void
TimelineViewManager::openImageOverlay(QString mxcUrl, QString eventId) const
{
        if (mxcUrl.isEmpty()) {
                return;
        }
        QQuickImageResponse *imgResponse =
          imgProvider->requestImageResponse(mxcUrl.remove("mxc://"), QSize());
        connect(imgResponse, &QQuickImageResponse::finished, this, [this, eventId, imgResponse]() {
                if (!imgResponse->errorString().isEmpty()) {
                        nhlog::ui()->error("Error when retrieving image for overlay: {}",
                                           imgResponse->errorString().toStdString());
                        return;
                }
                auto pixmap = QPixmap::fromImage(imgResponse->textureFactory()->image());

                auto imgDialog = new dialogs::ImageOverlay(pixmap);
                imgDialog->showFullScreen();
                connect(imgDialog,
                        &dialogs::ImageOverlay::saving,
                        timeline_,
                        [this, eventId, imgDialog]() {
                                // hide the overlay while presenting the save dialog for better
                                // cross platform support.
                                imgDialog->hide();

                                if (!timeline_->saveMedia(eventId)) {
                                        imgDialog->show();
                                } else {
                                        imgDialog->close();
                                }
                        });
        });
}

void
TimelineViewManager::openLink(QString link) const
{
        QDesktopServices::openUrl(link);
}

void
TimelineViewManager::openInviteUsersDialog()
{
        MainWindow::instance()->openInviteUsersDialog(
          [this](const QStringList &invitees) { emit inviteUsers(invitees); });
}
void
TimelineViewManager::openMemberListDialog() const
{
        MainWindow::instance()->openMemberListDialog(timeline_->roomId());
}
void
TimelineViewManager::openLeaveRoomDialog() const
{
        MainWindow::instance()->openLeaveRoomDialog(timeline_->roomId());
}

void
TimelineViewManager::verifyUser(QString userid)
{
        auto joined_rooms = cache::joinedRooms();
        auto room_infos   = cache::getRoomInfo(joined_rooms);

        for (std::string room_id : joined_rooms) {
                if ((room_infos[QString::fromStdString(room_id)].member_count == 2) &&
                    cache::isRoomEncrypted(room_id)) {
                        auto room_members = cache::roomMembers(room_id);
                        if (std::find(room_members.begin(),
                                      room_members.end(),
                                      (userid).toStdString()) != room_members.end()) {
                                auto model = models.value(QString::fromStdString(room_id));
                                auto flow  = DeviceVerificationFlow::InitiateUserVerification(
                                  this, model.data(), userid);
                                connect(model.data(),
                                        &TimelineModel::updateFlowEventId,
                                        this,
                                        [this, flow](std::string eventId) {
                                                dvList[QString::fromStdString(eventId)] = flow;
                                        });
                                emit newDeviceVerificationRequest(flow.data());
                                return;
                        }
                }
        }

        emit ChatPage::instance()->showNotification(
          tr("No encrypted private chat found with this user. Create an "
             "encrypted private chat with this user and try again."));
}

void
TimelineViewManager::removeVerificationFlow(DeviceVerificationFlow *flow)
{
        for (auto it = dvList.keyValueBegin(); it != dvList.keyValueEnd(); ++it) {
                if ((*it).second == flow) {
                        dvList.remove((*it).first);
                        return;
                }
        }
}

void
TimelineViewManager::verifyDevice(QString userid, QString deviceid)
{
        auto flow = DeviceVerificationFlow::InitiateDeviceVerification(this, userid, deviceid);
        this->dvList[flow->transactionId()] = flow;
        emit newDeviceVerificationRequest(flow.data());
}

void
TimelineViewManager::updateReadReceipts(const QString &room_id,
                                        const std::vector<QString> &event_ids)
{
        auto room = models.find(room_id);
        if (room != models.end()) {
                room.value()->markEventsAsRead(event_ids);
        }
}

void
TimelineViewManager::receivedSessionKey(const std::string &room_id, const std::string &session_id)
{
        auto room = models.find(QString::fromStdString(room_id));
        if (room != models.end()) {
                room.value()->receivedSessionKey(session_id);
        }
}

void
TimelineViewManager::initWithMessages(const std::vector<QString> &roomIds)
{
        for (const auto &roomId : roomIds)
                addRoom(roomId);
}

void
TimelineViewManager::queueReply(const QString &roomid,
                                const QString &repliedToEvent,
                                const QString &replyBody)
{
        auto room = models.find(roomid);
        if (room != models.end()) {
                room.value()->setReply(repliedToEvent);
                room.value()->input()->message(replyBody);
        }
}

void
TimelineViewManager::queueReactionMessage(const QString &reactedEvent, const QString &reactionKey)
{
        if (!timeline_)
                return;

        auto reactions = timeline_->reactions(reactedEvent.toStdString());

        QString selfReactedEvent;
        for (const auto &reaction : reactions) {
                if (reactionKey == reaction.key_) {
                        selfReactedEvent = reaction.selfReactedEvent_;
                        break;
                }
        }

        if (selfReactedEvent.startsWith("m"))
                return;

        // If selfReactedEvent is empty, that means we haven't previously reacted
        if (selfReactedEvent.isEmpty()) {
                mtx::events::msg::Reaction reaction;
                mtx::common::Relation rel;
                rel.rel_type = mtx::common::RelationType::Annotation;
                rel.event_id = reactedEvent.toStdString();
                rel.key      = reactionKey.toStdString();
                reaction.relations.relations.push_back(rel);

                timeline_->sendMessageEvent(reaction, mtx::events::EventType::Reaction);
                // Otherwise, we have previously reacted and the reaction should be redacted
        } else {
                timeline_->redactEvent(selfReactedEvent);
        }
}
void
TimelineViewManager::queueCallMessage(const QString &roomid,
                                      const mtx::events::msg::CallInvite &callInvite)
{
        models.value(roomid)->sendMessageEvent(callInvite, mtx::events::EventType::CallInvite);
}

void
TimelineViewManager::queueCallMessage(const QString &roomid,
                                      const mtx::events::msg::CallCandidates &callCandidates)
{
        models.value(roomid)->sendMessageEvent(callCandidates,
                                               mtx::events::EventType::CallCandidates);
}

void
TimelineViewManager::queueCallMessage(const QString &roomid,
                                      const mtx::events::msg::CallAnswer &callAnswer)
{
        models.value(roomid)->sendMessageEvent(callAnswer, mtx::events::EventType::CallAnswer);
}

void
TimelineViewManager::queueCallMessage(const QString &roomid,
                                      const mtx::events::msg::CallHangUp &callHangUp)
{
        models.value(roomid)->sendMessageEvent(callHangUp, mtx::events::EventType::CallHangUp);
}

void
TimelineViewManager::focusMessageInput()
{
        emit focusInput();
}
