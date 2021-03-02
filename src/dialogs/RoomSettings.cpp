#include "dialogs/RoomSettings.h"
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFontDatabase>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QShortcut>
#include <QShowEvent>
#include <QStandardPaths>
#include <QStyleOption>
#include <QVBoxLayout>
#include <mtx/responses/common.hpp>
#include <mtx/responses/media.hpp>

#include "Cache.h"
#include "ChatPage.h"
#include "Config.h"
#include "Logging.h"
#include "MatrixClient.h"
#include "Utils.h"
#include "ui/Avatar.h"
#include "ui/FlatButton.h"
#include "ui/LoadingIndicator.h"
#include "ui/Painter.h"
#include "ui/TextField.h"
#include "ui/ToggleButton.h"

using namespace dialogs;
using namespace mtx::events;

constexpr int BUTTON_SIZE       = 36;
constexpr int BUTTON_RADIUS     = BUTTON_SIZE / 2;
constexpr int WIDGET_MARGIN     = 20;
constexpr int TOP_WIDGET_MARGIN = 2 * WIDGET_MARGIN;
constexpr int WIDGET_SPACING    = 15;
constexpr int TEXT_SPACING      = 4;
constexpr int BUTTON_SPACING    = 2 * TEXT_SPACING;

bool
ClickableFilter::eventFilter(QObject *obj, QEvent *event)
{
        if (event->type() == QEvent::MouseButtonRelease) {
                emit clicked();
                return true;
        }

        return QObject::eventFilter(obj, event);
}

EditModal::EditModal(const QString &roomId, QWidget *parent)
  : QWidget(parent)
  , roomId_{roomId}
{
        setAutoFillBackground(true);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        setWindowModality(Qt::WindowModal);

        QFont largeFont;
        largeFont.setPointSizeF(largeFont.pointSizeF() * 1.4);
        setMinimumWidth(conf::window::minModalWidth);
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

        auto layout = new QVBoxLayout(this);

        applyBtn_  = new QPushButton(tr("Apply"), this);
        cancelBtn_ = new QPushButton(tr("Cancel"), this);
        cancelBtn_->setDefault(true);

        auto btnLayout = new QHBoxLayout;
        btnLayout->addStretch(1);
        btnLayout->setSpacing(15);
        btnLayout->addWidget(cancelBtn_);
        btnLayout->addWidget(applyBtn_);

        nameInput_ = new TextField(this);
        nameInput_->setLabel(tr("Name").toUpper());
        topicInput_ = new TextField(this);
        topicInput_->setLabel(tr("Topic").toUpper());

        errorField_ = new QLabel(this);
        errorField_->setWordWrap(true);
        errorField_->hide();

        layout->addWidget(nameInput_);
        layout->addWidget(topicInput_);
        layout->addLayout(btnLayout, 1);

        auto labelLayout = new QHBoxLayout;
        labelLayout->setAlignment(Qt::AlignHCenter);
        labelLayout->addWidget(errorField_);
        layout->addLayout(labelLayout);

        connect(applyBtn_, &QPushButton::clicked, this, &EditModal::applyClicked);
        connect(cancelBtn_, &QPushButton::clicked, this, &EditModal::close);

        auto window = QApplication::activeWindow();
        auto center = window->frameGeometry().center();
        move(center.x() - (width() * 0.5), center.y() - (height() * 0.5));
}

void
EditModal::topicEventSent()
{
        errorField_->hide();
        close();
}

void
EditModal::nameEventSent(const QString &name)
{
        errorField_->hide();
        emit nameChanged(name);
        close();
}

void
EditModal::error(const QString &msg)
{
        errorField_->setText(msg);
        errorField_->show();
}

void
EditModal::applyClicked()
{
        // Check if the values are changed from the originals.
        auto newName  = nameInput_->text().trimmed();
        auto newTopic = topicInput_->text().trimmed();

        errorField_->hide();

        if (newName == initialName_ && newTopic == initialTopic_) {
                close();
                return;
        }

        using namespace mtx::events;
        auto proxy = std::make_shared<ThreadProxy>();
        connect(proxy.get(), &ThreadProxy::topicEventSent, this, &EditModal::topicEventSent);
        connect(proxy.get(), &ThreadProxy::nameEventSent, this, &EditModal::nameEventSent);
        connect(proxy.get(), &ThreadProxy::error, this, &EditModal::error);

        if (newName != initialName_ && !newName.isEmpty()) {
                state::Name body;
                body.name = newName.toStdString();

                http::client()->send_state_event(
                  roomId_.toStdString(),
                  body,
                  [proxy, newName](const mtx::responses::EventId &, mtx::http::RequestErr err) {
                          if (err) {
                                  emit proxy->error(
                                    QString::fromStdString(err->matrix_error.error));
                                  return;
                          }

                          emit proxy->nameEventSent(newName);
                  });
        }

        if (newTopic != initialTopic_ && !newTopic.isEmpty()) {
                state::Topic body;
                body.topic = newTopic.toStdString();

                http::client()->send_state_event(
                  roomId_.toStdString(),
                  body,
                  [proxy](const mtx::responses::EventId &, mtx::http::RequestErr err) {
                          if (err) {
                                  emit proxy->error(
                                    QString::fromStdString(err->matrix_error.error));
                                  return;
                          }

                          emit proxy->topicEventSent();
                  });
        }
}

void
EditModal::setFields(const QString &roomName, const QString &roomTopic)
{
        initialName_  = roomName;
        initialTopic_ = roomTopic;

        nameInput_->setText(roomName);
        topicInput_->setText(roomTopic);
}

RoomSettings::RoomSettings(const QString &room_id, QWidget *parent)
  : QFrame(parent)
  , room_id_{std::move(room_id)}
{
        retrieveRoomInfo();

        setAutoFillBackground(true);
        setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
        setWindowModality(Qt::WindowModal);
        setAttribute(Qt::WA_DeleteOnClose, true);

        QFont largeFont;
        largeFont.setPointSizeF(largeFont.pointSizeF() * 1.5);

        setMinimumWidth(conf::window::minModalWidth);
        setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);

        auto layout = new QVBoxLayout(this);
        layout->setSpacing(WIDGET_SPACING);
        layout->setContentsMargins(WIDGET_MARGIN, TOP_WIDGET_MARGIN, WIDGET_MARGIN, WIDGET_MARGIN);

        QFont font;
        font.setWeight(QFont::Medium);
        auto settingsLabel = new QLabel(tr("Settings").toUpper(), this);
        settingsLabel->setFont(font);

        auto infoLabel = new QLabel(tr("Info").toUpper(), this);
        infoLabel->setFont(font);

        QFont monospaceFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);

        auto roomIdLabel = new QLabel(room_id, this);
        roomIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        roomIdLabel->setFont(monospaceFont);

        auto roomIdLayout = new QHBoxLayout;
        roomIdLayout->setMargin(0);
        roomIdLayout->addWidget(new QLabel(tr("Internal ID"), this),
                                Qt::AlignBottom | Qt::AlignLeft);
        roomIdLayout->addWidget(roomIdLabel, 0, Qt::AlignBottom | Qt::AlignRight);

        auto roomVersionLabel = new QLabel(QString::fromStdString(info_.version), this);
        roomVersionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        roomVersionLabel->setFont(monospaceFont);

        auto roomVersionLayout = new QHBoxLayout;
        roomVersionLayout->setMargin(0);
        roomVersionLayout->addWidget(new QLabel(tr("Room Version"), this),
                                     Qt::AlignBottom | Qt::AlignLeft);
        roomVersionLayout->addWidget(roomVersionLabel, 0, Qt::AlignBottom | Qt::AlignRight);

        auto notifLabel = new QLabel(tr("Notifications"), this);
        notifCombo      = new QComboBox(this);
        notifCombo->addItem(tr(
          "Muted")); //{"conditions":[{"kind":"event_match","key":"room_id","pattern":"!jxlRxnrZCsjpjDubDX:matrix.org"}],"actions":["dont_notify"]}
        notifCombo->addItem(tr("Mentions only")); // {"actions":["dont_notify"]}
        notifCombo->addItem(tr("All messages"));  // delete rule

        connect(this, &RoomSettings::notifChanged, notifCombo, &QComboBox::setCurrentIndex);
        http::client()->get_pushrules(
          "global",
          "override",
          room_id_.toStdString(),
          [this](const mtx::pushrules::PushRule &rule, mtx::http::RequestErr &err) {
                  if (err) {
                          if (err->status_code == boost::beast::http::status::not_found)
                                  http::client()->get_pushrules(
                                    "global",
                                    "room",
                                    room_id_.toStdString(),
                                    [this](const mtx::pushrules::PushRule &rule,
                                           mtx::http::RequestErr &err) {
                                            if (err) {
                                                    emit notifChanged(2); // all messages
                                                    return;
                                            }

                                            if (rule.enabled)
                                                    emit notifChanged(1); // mentions only
                                    });
                          return;
                  }

                  if (rule.enabled)
                          emit notifChanged(0); // muted
                  else
                          emit notifChanged(2); // all messages
          });

        connect(notifCombo, QOverload<int>::of(&QComboBox::activated), [this](int index) {
                std::string room_id = room_id_.toStdString();
                if (index == 0) {
                        // mute room
                        // delete old rule first, then add new rule
                        mtx::pushrules::PushRule rule;
                        rule.actions = {mtx::pushrules::actions::dont_notify{}};
                        mtx::pushrules::PushCondition condition;
                        condition.kind    = "event_match";
                        condition.key     = "room_id";
                        condition.pattern = room_id;
                        rule.conditions   = {condition};

                        http::client()->put_pushrules(
                          "global",
                          "override",
                          room_id,
                          rule,
                          [room_id](mtx::http::RequestErr &err) {
                                  if (err)
                                          nhlog::net()->error(
                                            "failed to set pushrule for room {}: {} {}",
                                            room_id,
                                            static_cast<int>(err->status_code),
                                            err->matrix_error.error);
                                  http::client()->delete_pushrules(
                                    "global", "room", room_id, [room_id](mtx::http::RequestErr &) {
                                    });
                          });
                } else if (index == 1) {
                        // mentions only
                        // delete old rule first, then add new rule
                        mtx::pushrules::PushRule rule;
                        rule.actions = {mtx::pushrules::actions::dont_notify{}};
                        http::client()->put_pushrules(
                          "global", "room", room_id, rule, [room_id](mtx::http::RequestErr &err) {
                                  if (err)
                                          nhlog::net()->error(
                                            "failed to set pushrule for room {}: {} {}",
                                            room_id,
                                            static_cast<int>(err->status_code),
                                            err->matrix_error.error);
                                  http::client()->delete_pushrules(
                                    "global",
                                    "override",
                                    room_id,
                                    [room_id](mtx::http::RequestErr &) {});
                          });
                } else {
                        // all messages
                        http::client()->delete_pushrules(
                          "global", "override", room_id, [room_id](mtx::http::RequestErr &) {
                                  http::client()->delete_pushrules(
                                    "global", "room", room_id, [room_id](mtx::http::RequestErr &) {
                                    });
                          });
                }
        });

        auto notifOptionLayout_ = new QHBoxLayout;
        notifOptionLayout_->setMargin(0);
        notifOptionLayout_->addWidget(notifLabel, Qt::AlignBottom | Qt::AlignLeft);
        notifOptionLayout_->addWidget(notifCombo, 0, Qt::AlignBottom | Qt::AlignRight);

        auto accessLabel = new QLabel(tr("Room access"), this);
        accessCombo      = new QComboBox(this);
        accessCombo->addItem(tr("Anyone and guests"));
        accessCombo->addItem(tr("Anyone"));
        accessCombo->addItem(tr("Invited users"));
        accessCombo->setDisabled(
          !canChangeJoinRules(room_id_.toStdString(), utils::localUser().toStdString()));
        connect(accessCombo, QOverload<int>::of(&QComboBox::activated), [this](int index) {
                using namespace mtx::events::state;

                auto guest_access = [](int index) -> state::GuestAccess {
                        state::GuestAccess event;

                        if (index == 0)
                                event.guest_access = state::AccessState::CanJoin;
                        else
                                event.guest_access = state::AccessState::Forbidden;

                        return event;
                }(index);

                auto join_rule = [](int index) -> state::JoinRules {
                        state::JoinRules event;

                        switch (index) {
                        case 0:
                        case 1:
                                event.join_rule = state::JoinRule::Public;
                                break;
                        default:
                                event.join_rule = state::JoinRule::Invite;
                        }

                        return event;
                }(index);

                updateAccessRules(room_id_.toStdString(), join_rule, guest_access);
        });

        if (info_.join_rule == state::JoinRule::Public) {
                if (info_.guest_access) {
                        accessCombo->setCurrentIndex(0);
                } else {
                        accessCombo->setCurrentIndex(1);
                }
        } else {
                accessCombo->setCurrentIndex(2);
        }

        auto accessOptionLayout = new QHBoxLayout();
        accessOptionLayout->setMargin(0);
        accessOptionLayout->addWidget(accessLabel, Qt::AlignBottom | Qt::AlignLeft);
        accessOptionLayout->addWidget(accessCombo, 0, Qt::AlignBottom | Qt::AlignRight);

        auto encryptionLabel = new QLabel(tr("Encryption"), this);
        encryptionToggle_    = new Toggle(this);

        auto encryptionOptionLayout = new QHBoxLayout;
        encryptionOptionLayout->setMargin(0);
        encryptionOptionLayout->addWidget(encryptionLabel, Qt::AlignBottom | Qt::AlignLeft);
        encryptionOptionLayout->addWidget(encryptionToggle_, 0, Qt::AlignBottom | Qt::AlignRight);

        auto keyRequestsLabel = new QLabel(tr("Respond to key requests"), this);
        keyRequestsLabel->setToolTipDuration(6000);
        keyRequestsLabel->setToolTip(
          tr("Whether or not the client should respond automatically with the session keys\n"
             " upon request. Use with caution, this is a temporary measure to test the\n"
             " E2E implementation until device verification is completed."));
        keyRequestsToggle_ = new Toggle(this);
        connect(keyRequestsToggle_, &Toggle::toggled, this, [this](bool isOn) {
                utils::setKeyRequestsPreference(room_id_, isOn);
        });

        auto keyRequestsLayout = new QHBoxLayout;
        keyRequestsLayout->setMargin(0);
        keyRequestsLayout->setSpacing(0);
        keyRequestsLayout->addWidget(keyRequestsLabel, Qt::AlignBottom | Qt::AlignLeft);
        keyRequestsLayout->addWidget(keyRequestsToggle_, 0, Qt::AlignBottom | Qt::AlignRight);

        connect(encryptionToggle_, &Toggle::toggled, this, [this, keyRequestsLabel](bool isOn) {
                if (!isOn || usesEncryption_)
                        return;

                QMessageBox msgBox;
                msgBox.setIcon(QMessageBox::Question);
                msgBox.setWindowTitle(tr("End-to-End Encryption"));
                msgBox.setText(tr(
                  "Encryption is currently experimental and things might break unexpectedly. <br>"
                  "Please take note that it can't be disabled afterwards."));
                msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
                msgBox.setDefaultButton(QMessageBox::Save);
                int ret = msgBox.exec();

                switch (ret) {
                case QMessageBox::Ok: {
                        encryptionToggle_->setState(true);
                        encryptionToggle_->setEnabled(false);
                        enableEncryption();
                        keyRequestsToggle_->show();
                        keyRequestsLabel->show();
                        break;
                }
                default: {
                        break;
                }
                }
        });

        // Disable encryption button.
        if (usesEncryption_) {
                encryptionToggle_->setState(true);
                encryptionToggle_->setEnabled(false);

                keyRequestsToggle_->setState(utils::respondsToKeyRequests(room_id_));
        } else {
                encryptionToggle_->setState(false);

                keyRequestsLabel->hide();
                keyRequestsToggle_->hide();
        }

        // Hide encryption option for public rooms.
        if (!usesEncryption_ && (info_.join_rule == state::JoinRule::Public)) {
                encryptionToggle_->hide();
                encryptionLabel->hide();

                keyRequestsLabel->hide();
                keyRequestsToggle_->hide();
        }

        avatar_ = new Avatar(this, 128);
        avatar_->setFiller(QString::fromStdString(info_.name));
        if (!info_.avatar_url.empty())
                avatar_->setImage(QString::fromStdString(info_.avatar_url));

        if (canChangeAvatar(room_id_.toStdString(), utils::localUser().toStdString())) {
                auto filter = new ClickableFilter(this);
                avatar_->installEventFilter(filter);
                avatar_->setCursor(Qt::PointingHandCursor);
                connect(filter, &ClickableFilter::clicked, this, &RoomSettings::updateAvatar);
        }

        roomNameLabel_ = new QLabel(QString::fromStdString(info_.name), this);
        roomNameLabel_->setFont(largeFont);

        auto membersLabel = new QLabel(tr("%n member(s)", "", (int)info_.member_count), this);

        auto textLayout = new QVBoxLayout;
        textLayout->addWidget(roomNameLabel_);
        textLayout->addWidget(membersLabel);
        textLayout->setAlignment(roomNameLabel_, Qt::AlignCenter | Qt::AlignTop);
        textLayout->setAlignment(membersLabel, Qt::AlignCenter | Qt::AlignTop);
        textLayout->setSpacing(TEXT_SPACING);
        textLayout->setMargin(0);

        setupEditButton();

        errorLabel_ = new QLabel(this);
        errorLabel_->setAlignment(Qt::AlignCenter);
        errorLabel_->hide();

        spinner_ = new LoadingIndicator(this);
        spinner_->setFixedHeight(30);
        spinner_->setFixedWidth(30);
        spinner_->hide();
        auto spinnerLayout = new QVBoxLayout;
        spinnerLayout->addWidget(spinner_);
        spinnerLayout->setAlignment(Qt::AlignCenter);
        spinnerLayout->setMargin(0);
        spinnerLayout->setSpacing(0);

        auto okBtn = new QPushButton("OK", this);

        auto buttonLayout = new QHBoxLayout();
        buttonLayout->setSpacing(15);
        buttonLayout->addStretch(1);
        buttonLayout->addWidget(okBtn);

        layout->addWidget(avatar_, Qt::AlignCenter | Qt::AlignTop);
        layout->addLayout(textLayout);
        layout->addLayout(btnLayout_);
        layout->addWidget(settingsLabel, Qt::AlignLeft);
        layout->addLayout(notifOptionLayout_);
        layout->addLayout(accessOptionLayout);
        layout->addLayout(encryptionOptionLayout);
        layout->addLayout(keyRequestsLayout);
        layout->addWidget(infoLabel, Qt::AlignLeft);
        layout->addLayout(roomIdLayout);
        layout->addLayout(roomVersionLayout);
        layout->addWidget(errorLabel_);
        layout->addLayout(buttonLayout);
        layout->addLayout(spinnerLayout);
        layout->addStretch(1);

        connect(this, &RoomSettings::enableEncryptionError, this, [this](const QString &msg) {
                encryptionToggle_->setState(false);
                keyRequestsToggle_->setState(false);
                keyRequestsToggle_->setEnabled(false);
                keyRequestsToggle_->hide();

                emit ChatPage::instance()->showNotification(msg);
        });

        connect(this, &RoomSettings::showErrorMessage, this, [this](const QString &msg) {
                if (!errorLabel_)
                        return;

                stopLoadingSpinner();

                errorLabel_->show();
                errorLabel_->setText(msg);
        });

        connect(this, &RoomSettings::accessRulesUpdated, this, [this]() {
                stopLoadingSpinner();
                resetErrorLabel();
        });

        auto closeShortcut = new QShortcut(QKeySequence(QKeySequence::Cancel), this);
        connect(closeShortcut, &QShortcut::activated, this, &RoomSettings::close);
        connect(okBtn, &QPushButton::clicked, this, &RoomSettings::close);
}

void
RoomSettings::setupEditButton()
{
        btnLayout_ = new QHBoxLayout;
        btnLayout_->setSpacing(BUTTON_SPACING);
        btnLayout_->setMargin(0);

        if (!canChangeNameAndTopic(room_id_.toStdString(), utils::localUser().toStdString()))
                return;

        QIcon editIcon;
        editIcon.addFile(":/icons/icons/ui/edit.png");
        editFieldsBtn_ = new FlatButton(this);
        editFieldsBtn_->setFixedSize(BUTTON_SIZE, BUTTON_SIZE);
        editFieldsBtn_->setCornerRadius(BUTTON_RADIUS);
        editFieldsBtn_->setIcon(editIcon);
        editFieldsBtn_->setIcon(editIcon);
        editFieldsBtn_->setIconSize(QSize(BUTTON_RADIUS, BUTTON_RADIUS));

        connect(editFieldsBtn_, &QPushButton::clicked, this, [this]() {
                retrieveRoomInfo();

                auto modal = new EditModal(room_id_, this);
                modal->setFields(QString::fromStdString(info_.name),
                                 QString::fromStdString(info_.topic));
                modal->raise();
                modal->show();
                connect(modal, &EditModal::nameChanged, this, [this](const QString &newName) {
                        if (roomNameLabel_)
                                roomNameLabel_->setText(newName);
                });
        });

        btnLayout_->addStretch(1);
        btnLayout_->addWidget(editFieldsBtn_);
        btnLayout_->addStretch(1);
}

void
RoomSettings::retrieveRoomInfo()
{
        try {
                usesEncryption_ = cache::isRoomEncrypted(room_id_.toStdString());
                info_           = cache::singleRoomInfo(room_id_.toStdString());
                setAvatar();
        } catch (const lmdb::error &) {
                nhlog::db()->warn("failed to retrieve room info from cache: {}",
                                  room_id_.toStdString());
        }
}

void
RoomSettings::enableEncryption()
{
        const auto room_id = room_id_.toStdString();
        http::client()->enable_encryption(
          room_id, [room_id, this](const mtx::responses::EventId &, mtx::http::RequestErr err) {
                  if (err) {
                          int status_code = static_cast<int>(err->status_code);
                          nhlog::net()->warn("failed to enable encryption in room ({}): {} {}",
                                             room_id,
                                             err->matrix_error.error,
                                             status_code);
                          emit enableEncryptionError(
                            tr("Failed to enable encryption: %1")
                              .arg(QString::fromStdString(err->matrix_error.error)));
                          return;
                  }

                  nhlog::net()->info("enabled encryption on room ({})", room_id);
          });
}

void
RoomSettings::showEvent(QShowEvent *event)
{
        resetErrorLabel();
        stopLoadingSpinner();

        QWidget::showEvent(event);
}

bool
RoomSettings::canChangeJoinRules(const std::string &room_id, const std::string &user_id) const
{
        try {
                return cache::hasEnoughPowerLevel({EventType::RoomJoinRules}, room_id, user_id);
        } catch (const lmdb::error &e) {
                nhlog::db()->warn("lmdb error: {}", e.what());
        }

        return false;
}

bool
RoomSettings::canChangeNameAndTopic(const std::string &room_id, const std::string &user_id) const
{
        try {
                return cache::hasEnoughPowerLevel(
                  {EventType::RoomName, EventType::RoomTopic}, room_id, user_id);
        } catch (const lmdb::error &e) {
                nhlog::db()->warn("lmdb error: {}", e.what());
        }

        return false;
}

bool
RoomSettings::canChangeAvatar(const std::string &room_id, const std::string &user_id) const
{
        try {
                return cache::hasEnoughPowerLevel({EventType::RoomAvatar}, room_id, user_id);
        } catch (const lmdb::error &e) {
                nhlog::db()->warn("lmdb error: {}", e.what());
        }

        return false;
}

void
RoomSettings::updateAccessRules(const std::string &room_id,
                                const mtx::events::state::JoinRules &join_rule,
                                const mtx::events::state::GuestAccess &guest_access)
{
        startLoadingSpinner();
        resetErrorLabel();

        http::client()->send_state_event(
          room_id,
          join_rule,
          [this, room_id, guest_access](const mtx::responses::EventId &,
                                        mtx::http::RequestErr err) {
                  if (err) {
                          nhlog::net()->warn("failed to send m.room.join_rule: {} {}",
                                             static_cast<int>(err->status_code),
                                             err->matrix_error.error);
                          emit showErrorMessage(QString::fromStdString(err->matrix_error.error));

                          return;
                  }

                  http::client()->send_state_event(
                    room_id,
                    guest_access,
                    [this](const mtx::responses::EventId &, mtx::http::RequestErr err) {
                            if (err) {
                                    nhlog::net()->warn("failed to send m.room.guest_access: {} {}",
                                                       static_cast<int>(err->status_code),
                                                       err->matrix_error.error);
                                    emit showErrorMessage(
                                      QString::fromStdString(err->matrix_error.error));

                                    return;
                            }

                            emit accessRulesUpdated();
                    });
          });
}

void
RoomSettings::stopLoadingSpinner()
{
        if (spinner_) {
                spinner_->stop();
                spinner_->hide();
        }
}

void
RoomSettings::startLoadingSpinner()
{
        if (spinner_) {
                spinner_->start();
                spinner_->show();
        }
}

void
RoomSettings::displayErrorMessage(const QString &msg)
{
        stopLoadingSpinner();

        errorLabel_->show();
        errorLabel_->setText(msg);
}

void
RoomSettings::setAvatar()
{
        stopLoadingSpinner();

        if (avatar_)
                avatar_->setImage(QString::fromStdString(info_.avatar_url));
}

void
RoomSettings::resetErrorLabel()
{
        if (errorLabel_) {
                errorLabel_->hide();
                errorLabel_->clear();
        }
}

void
RoomSettings::updateAvatar()
{
        const QString picturesFolder =
          QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString fileName = QFileDialog::getOpenFileName(
          this, tr("Select an avatar"), picturesFolder, tr("All Files (*)"));

        if (fileName.isEmpty())
                return;

        QMimeDatabase db;
        QMimeType mime = db.mimeTypeForFile(fileName, QMimeDatabase::MatchContent);

        const auto format = mime.name().split("/")[0];

        QFile file{fileName, this};
        if (format != "image") {
                displayErrorMessage(tr("The selected file is not an image"));
                return;
        }

        if (!file.open(QIODevice::ReadOnly)) {
                displayErrorMessage(tr("Error while reading file: %1").arg(file.errorString()));
                return;
        }

        if (spinner_) {
                startLoadingSpinner();
                resetErrorLabel();
        }

        // Events emitted from the http callbacks (different threads) will
        // be queued back into the UI thread through this proxy object.
        auto proxy = std::make_shared<ThreadProxy>();
        connect(proxy.get(), &ThreadProxy::error, this, &RoomSettings::displayErrorMessage);
        connect(proxy.get(), &ThreadProxy::avatarChanged, this, &RoomSettings::setAvatar);

        const auto bin        = file.peek(file.size());
        const auto payload    = std::string(bin.data(), bin.size());
        const auto dimensions = QImageReader(&file).size();

        // First we need to create a new mxc URI
        // (i.e upload media to the Matrix content repository) for the new avatar.
        http::client()->upload(
          payload,
          mime.name().toStdString(),
          QFileInfo(fileName).fileName().toStdString(),
          [proxy = std::move(proxy),
           dimensions,
           payload,
           mimetype = mime.name().toStdString(),
           size     = payload.size(),
           room_id  = room_id_.toStdString(),
           content  = std::move(bin)](const mtx::responses::ContentURI &res,
                                     mtx::http::RequestErr err) {
                  if (err) {
                          emit proxy->error(
                            tr("Failed to upload image: %s")
                              .arg(QString::fromStdString(err->matrix_error.error)));
                          return;
                  }

                  using namespace mtx::events;
                  state::Avatar avatar_event;
                  avatar_event.image_info.w        = dimensions.width();
                  avatar_event.image_info.h        = dimensions.height();
                  avatar_event.image_info.mimetype = mimetype;
                  avatar_event.image_info.size     = size;
                  avatar_event.url                 = res.content_uri;

                  http::client()->send_state_event(
                    room_id,
                    avatar_event,
                    [content = std::move(content), proxy = std::move(proxy)](
                      const mtx::responses::EventId &, mtx::http::RequestErr err) {
                            if (err) {
                                    emit proxy->error(
                                      tr("Failed to upload image: %s")
                                        .arg(QString::fromStdString(err->matrix_error.error)));
                                    return;
                            }

                            emit proxy->avatarChanged();
                    });
          });
}
