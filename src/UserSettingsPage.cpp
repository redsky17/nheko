/*
 * nheko Copyright (C) 2017  Konstantinos Sideris <siderisk@auth.gr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScroller>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>
#include <QtQml>

#include "Cache.h"
#include "CallDevices.h"
#include "Config.h"
#include "MatrixClient.h"
#include "Olm.h"
#include "UserSettingsPage.h"
#include "Utils.h"
#include "ui/FlatButton.h"
#include "ui/ToggleButton.h"

#include "config/nheko.h"

QSharedPointer<UserSettings> UserSettings::instance_;

UserSettings::UserSettings()
{
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, []() {
                instance_.clear();
        });
}

QSharedPointer<UserSettings>
UserSettings::instance()
{
        return instance_;
}

void
UserSettings::initialize(std::optional<QString> profile)
{
        instance_.reset(new UserSettings());
        instance_->load(profile);
}

void
UserSettings::load(std::optional<QString> profile)
{
        QSettings settings;
        tray_                    = settings.value("user/window/tray", false).toBool();
        hasDesktopNotifications_ = settings.value("user/desktop_notifications", true).toBool();
        hasAlertOnNotification_  = settings.value("user/alert_on_notification", false).toBool();
        startInTray_             = settings.value("user/window/start_in_tray", false).toBool();
        groupView_               = settings.value("user/group_view", true).toBool();
        hiddenTags_              = settings.value("user/hidden_tags", QStringList{}).toStringList();
        buttonsInTimeline_       = settings.value("user/timeline/buttons", true).toBool();
        timelineMaxWidth_        = settings.value("user/timeline/max_width", 0).toInt();
        messageHoverHighlight_ =
          settings.value("user/timeline/message_hover_highlight", false).toBool();
        enlargeEmojiOnlyMessages_ =
          settings.value("user/timeline/enlarge_emoji_only_msg", false).toBool();
        markdown_             = settings.value("user/markdown_enabled", true).toBool();
        typingNotifications_  = settings.value("user/typing_notifications", true).toBool();
        sortByImportance_     = settings.value("user/sort_by_unread", true).toBool();
        readReceipts_         = settings.value("user/read_receipts", true).toBool();
        theme_                = settings.value("user/theme", defaultTheme_).toString();
        font_                 = settings.value("user/font_family", "default").toString();
        avatarCircles_        = settings.value("user/avatar_circles", true).toBool();
        useIdenticon_         = settings.value("user/use_identicon", true).toBool();
        decryptSidebar_       = settings.value("user/decrypt_sidebar", true).toBool();
        privacyScreen_        = settings.value("user/privacy_screen", false).toBool();
        privacyScreenTimeout_ = settings.value("user/privacy_screen_timeout", 0).toInt();
        shareKeysWithTrustedUsers_ =
          settings.value("user/share_keys_with_trusted_users", true).toBool();
        mobileMode_        = settings.value("user/mobile_mode", false).toBool();
        emojiFont_         = settings.value("user/emoji_font_family", "default").toString();
        baseFontSize_      = settings.value("user/font_size", QFont().pointSizeF()).toDouble();
        auto tempPresence  = settings.value("user/presence", "").toString().toStdString();
        auto presenceValue = QMetaEnum::fromType<Presence>().keyToValue(tempPresence.c_str());
        if (presenceValue < 0)
                presenceValue = 0;
        presence_               = static_cast<Presence>(presenceValue);
        ringtone_               = settings.value("user/ringtone", "Default").toString();
        microphone_             = settings.value("user/microphone", QString()).toString();
        camera_                 = settings.value("user/camera", QString()).toString();
        cameraResolution_       = settings.value("user/camera_resolution", QString()).toString();
        cameraFrameRate_        = settings.value("user/camera_frame_rate", QString()).toString();
        screenShareFrameRate_   = settings.value("user/screen_share_frame_rate", 5).toInt();
        screenSharePiP_         = settings.value("user/screen_share_pip", true).toBool();
        screenShareRemoteVideo_ = settings.value("user/screen_share_remote_video", false).toBool();
        screenShareHideCursor_  = settings.value("user/screen_share_hide_cursor", false).toBool();
        useStunServer_          = settings.value("user/use_stun_server", false).toBool();

        if (profile) // set to "" if it's the default to maintain compatibility
                profile_ = (*profile == "default") ? "" : *profile;
        else
                profile_ = settings.value("user/currentProfile", "").toString();

        QString prefix =
          (profile_ != "" && profile_ != "default") ? "profile/" + profile_ + "/" : "";
        accessToken_ = settings.value(prefix + "auth/access_token", "").toString();
        homeserver_  = settings.value(prefix + "auth/home_server", "").toString();
        userId_      = settings.value(prefix + "auth/user_id", "").toString();
        deviceId_    = settings.value(prefix + "auth/device_id", "").toString();

        applyTheme();
}
void
UserSettings::setMessageHoverHighlight(bool state)
{
        if (state == messageHoverHighlight_)
                return;
        messageHoverHighlight_ = state;
        emit messageHoverHighlightChanged(state);
        save();
}
void
UserSettings::setEnlargeEmojiOnlyMessages(bool state)
{
        if (state == enlargeEmojiOnlyMessages_)
                return;
        enlargeEmojiOnlyMessages_ = state;
        emit enlargeEmojiOnlyMessagesChanged(state);
        save();
}
void
UserSettings::setTray(bool state)
{
        if (state == tray_)
                return;
        tray_ = state;
        emit trayChanged(state);
        save();
}

void
UserSettings::setStartInTray(bool state)
{
        if (state == startInTray_)
                return;
        startInTray_ = state;
        emit startInTrayChanged(state);
        save();
}

void
UserSettings::setMobileMode(bool state)
{
        if (state == mobileMode_)
                return;
        mobileMode_ = state;
        emit mobileModeChanged(state);
        save();
}

void
UserSettings::setGroupView(bool state)
{
        if (groupView_ != state)
                emit groupViewStateChanged(state);

        groupView_ = state;
        save();
}

void
UserSettings::setHiddenTags(QStringList hiddenTags)
{
        hiddenTags_ = hiddenTags;
        save();
}

void
UserSettings::setMarkdown(bool state)
{
        if (state == markdown_)
                return;
        markdown_ = state;
        emit markdownChanged(state);
        save();
}

void
UserSettings::setReadReceipts(bool state)
{
        if (state == readReceipts_)
                return;
        readReceipts_ = state;
        emit readReceiptsChanged(state);
        save();
}

void
UserSettings::setTypingNotifications(bool state)
{
        if (state == typingNotifications_)
                return;
        typingNotifications_ = state;
        emit typingNotificationsChanged(state);
        save();
}

void
UserSettings::setSortByImportance(bool state)
{
        if (state == sortByImportance_)
                return;
        sortByImportance_ = state;
        emit roomSortingChanged(state);
        save();
}

void
UserSettings::setButtonsInTimeline(bool state)
{
        if (state == buttonsInTimeline_)
                return;
        buttonsInTimeline_ = state;
        emit buttonInTimelineChanged(state);
        save();
}

void
UserSettings::setTimelineMaxWidth(int state)
{
        if (state == timelineMaxWidth_)
                return;
        timelineMaxWidth_ = state;
        emit timelineMaxWidthChanged(state);
        save();
}

void
UserSettings::setDesktopNotifications(bool state)
{
        if (state == hasDesktopNotifications_)
                return;
        hasDesktopNotifications_ = state;
        emit desktopNotificationsChanged(state);
        save();
}

void
UserSettings::setAlertOnNotification(bool state)
{
        if (state == hasAlertOnNotification_)
                return;
        hasAlertOnNotification_ = state;
        emit alertOnNotificationChanged(state);
        save();
}

void
UserSettings::setAvatarCircles(bool state)
{
        if (state == avatarCircles_)
                return;
        avatarCircles_ = state;
        emit avatarCirclesChanged(state);
        save();
}

void
UserSettings::setDecryptSidebar(bool state)
{
        if (state == decryptSidebar_)
                return;
        decryptSidebar_ = state;
        emit decryptSidebarChanged(state);
        save();
}

void
UserSettings::setPrivacyScreen(bool state)
{
        if (state == privacyScreen_) {
                return;
        }
        privacyScreen_ = state;
        emit privacyScreenChanged(state);
        save();
}

void
UserSettings::setPrivacyScreenTimeout(int state)
{
        if (state == privacyScreenTimeout_) {
                return;
        }
        privacyScreenTimeout_ = state;
        emit privacyScreenTimeoutChanged(state);
        save();
}

void
UserSettings::setFontSize(double size)
{
        if (size == baseFontSize_)
                return;
        baseFontSize_ = size;
        emit fontSizeChanged(size);
        save();
}

void
UserSettings::setFontFamily(QString family)
{
        if (family == font_)
                return;
        font_ = family;
        emit fontChanged(family);
        save();
}

void
UserSettings::setEmojiFontFamily(QString family)
{
        if (family == emojiFont_)
                return;

        if (family == tr("Default")) {
                emojiFont_ = "default";
        } else {
                emojiFont_ = family;
        }

        emit emojiFontChanged(family);
        save();
}

void
UserSettings::setPresence(Presence state)
{
        if (state == presence_)
                return;
        presence_ = state;
        emit presenceChanged(state);
        save();
}

void
UserSettings::setTheme(QString theme)
{
        if (theme == theme_)
                return;
        theme_ = theme;
        save();
        applyTheme();
        emit themeChanged(theme);
}

void
UserSettings::setUseStunServer(bool useStunServer)
{
        if (useStunServer == useStunServer_)
                return;
        useStunServer_ = useStunServer;
        emit useStunServerChanged(useStunServer);
        save();
}

void
UserSettings::setShareKeysWithTrustedUsers(bool shareKeys)
{
        if (shareKeys == shareKeysWithTrustedUsers_)
                return;

        shareKeysWithTrustedUsers_ = shareKeys;
        emit shareKeysWithTrustedUsersChanged(shareKeys);
        save();
}

void
UserSettings::setRingtone(QString ringtone)
{
        if (ringtone == ringtone_)
                return;
        ringtone_ = ringtone;
        emit ringtoneChanged(ringtone);
        save();
}

void
UserSettings::setMicrophone(QString microphone)
{
        if (microphone == microphone_)
                return;
        microphone_ = microphone;
        emit microphoneChanged(microphone);
        save();
}

void
UserSettings::setCamera(QString camera)
{
        if (camera == camera_)
                return;
        camera_ = camera;
        emit cameraChanged(camera);
        save();
}

void
UserSettings::setCameraResolution(QString resolution)
{
        if (resolution == cameraResolution_)
                return;
        cameraResolution_ = resolution;
        emit cameraResolutionChanged(resolution);
        save();
}

void
UserSettings::setCameraFrameRate(QString frameRate)
{
        if (frameRate == cameraFrameRate_)
                return;
        cameraFrameRate_ = frameRate;
        emit cameraFrameRateChanged(frameRate);
        save();
}

void
UserSettings::setScreenShareFrameRate(int frameRate)
{
        if (frameRate == screenShareFrameRate_)
                return;
        screenShareFrameRate_ = frameRate;
        emit screenShareFrameRateChanged(frameRate);
        save();
}

void
UserSettings::setScreenSharePiP(bool state)
{
        if (state == screenSharePiP_)
                return;
        screenSharePiP_ = state;
        emit screenSharePiPChanged(state);
        save();
}

void
UserSettings::setScreenShareRemoteVideo(bool state)
{
        if (state == screenShareRemoteVideo_)
                return;
        screenShareRemoteVideo_ = state;
        emit screenShareRemoteVideoChanged(state);
        save();
}

void
UserSettings::setScreenShareHideCursor(bool state)
{
        if (state == screenShareHideCursor_)
                return;
        screenShareHideCursor_ = state;
        emit screenShareHideCursorChanged(state);
        save();
}

void
UserSettings::setProfile(QString profile)
{
        if (profile == profile_)
                return;
        profile_ = profile;
        emit profileChanged(profile_);
        save();
}

void
UserSettings::setUserId(QString userId)
{
        if (userId == userId_)
                return;
        userId_ = userId;
        emit userIdChanged(userId_);
        save();
}

void
UserSettings::setAccessToken(QString accessToken)
{
        if (accessToken == accessToken_)
                return;
        accessToken_ = accessToken;
        emit accessTokenChanged(accessToken_);
        save();
}

void
UserSettings::setDeviceId(QString deviceId)
{
        if (deviceId == deviceId_)
                return;
        deviceId_ = deviceId;
        emit deviceIdChanged(deviceId_);
        save();
}

void
UserSettings::setHomeserver(QString homeserver)
{
        if (homeserver == homeserver_)
                return;
        homeserver_ = homeserver;
        emit homeserverChanged(homeserver_);
        save();
}

void
UserSettings::setUseIdenticon(bool state)
{
        if (state == useIdenticon_)
                return;
        useIdenticon_ = state;
        emit useIdenticonChanged(useIdenticon_);
        save();
}

void
UserSettings::applyTheme()
{
        QFile stylefile;

        static QPalette original;
        if (this->theme() == "light") {
                stylefile.setFileName(":/styles/styles/nheko.qss");
                QPalette lightActive(
                  /*windowText*/ QColor("#333"),
                  /*button*/ QColor("white"),
                  /*light*/ QColor(0xef, 0xef, 0xef),
                  /*dark*/ QColor(110, 110, 110),
                  /*mid*/ QColor(220, 220, 220),
                  /*text*/ QColor("#333"),
                  /*bright_text*/ QColor("#333"),
                  /*base*/ QColor("#fff"),
                  /*window*/ QColor("white"));
                lightActive.setColor(QPalette::AlternateBase, QColor("#eee"));
                lightActive.setColor(QPalette::Highlight, QColor("#38a3d8"));
                lightActive.setColor(QPalette::ToolTipBase, lightActive.base().color());
                lightActive.setColor(QPalette::ToolTipText, lightActive.text().color());
                lightActive.setColor(QPalette::Link, QColor("#0077b5"));
                lightActive.setColor(QPalette::ButtonText, QColor("#333"));
                QApplication::setPalette(lightActive);
        } else if (this->theme() == "dark") {
                stylefile.setFileName(":/styles/styles/nheko-dark.qss");
                QPalette darkActive(
                  /*windowText*/ QColor("#caccd1"),
                  /*button*/ QColor(0xff, 0xff, 0xff),
                  /*light*/ QColor("#caccd1"),
                  /*dark*/ QColor(110, 110, 110),
                  /*mid*/ QColor("#202228"),
                  /*text*/ QColor("#caccd1"),
                  /*bright_text*/ QColor(0xff, 0xff, 0xff),
                  /*base*/ QColor("#202228"),
                  /*window*/ QColor("#2d3139"));
                darkActive.setColor(QPalette::AlternateBase, QColor("#2d3139"));
                darkActive.setColor(QPalette::Highlight, QColor("#38a3d8"));
                darkActive.setColor(QPalette::ToolTipBase, darkActive.base().color());
                darkActive.setColor(QPalette::ToolTipText, darkActive.text().color());
                darkActive.setColor(QPalette::Link, QColor("#38a3d8"));
                darkActive.setColor(QPalette::ButtonText, "#727274");
                QApplication::setPalette(darkActive);
        } else {
                stylefile.setFileName(":/styles/styles/system.qss");
                QApplication::setPalette(original);
        }

        stylefile.open(QFile::ReadOnly);
        QString stylesheet = QString(stylefile.readAll());

        qobject_cast<QApplication *>(QApplication::instance())->setStyleSheet(stylesheet);
}

void
UserSettings::save()
{
        QSettings settings;
        settings.beginGroup("user");

        settings.beginGroup("window");
        settings.setValue("tray", tray_);
        settings.setValue("start_in_tray", startInTray_);
        settings.endGroup(); // window

        settings.beginGroup("timeline");
        settings.setValue("buttons", buttonsInTimeline_);
        settings.setValue("message_hover_highlight", messageHoverHighlight_);
        settings.setValue("enlarge_emoji_only_msg", enlargeEmojiOnlyMessages_);
        settings.setValue("max_width", timelineMaxWidth_);
        settings.endGroup(); // timeline

        settings.setValue("avatar_circles", avatarCircles_);
        settings.setValue("decrypt_sidebar", decryptSidebar_);
        settings.setValue("privacy_screen", privacyScreen_);
        settings.setValue("privacy_screen_timeout", privacyScreenTimeout_);
        settings.setValue("share_keys_with_trusted_users", shareKeysWithTrustedUsers_);
        settings.setValue("mobile_mode", mobileMode_);
        settings.setValue("font_size", baseFontSize_);
        settings.setValue("typing_notifications", typingNotifications_);
        settings.setValue("minor_events", sortByImportance_);
        settings.setValue("read_receipts", readReceipts_);
        settings.setValue("group_view", groupView_);
        settings.setValue("hidden_tags", hiddenTags_);
        settings.setValue("markdown_enabled", markdown_);
        settings.setValue("desktop_notifications", hasDesktopNotifications_);
        settings.setValue("alert_on_notification", hasAlertOnNotification_);
        settings.setValue("theme", theme());
        settings.setValue("font_family", font_);
        settings.setValue("emoji_font_family", emojiFont_);
        settings.setValue("presence",
                          QString::fromUtf8(QMetaEnum::fromType<Presence>().valueToKey(
                            static_cast<int>(presence_))));
        settings.setValue("ringtone", ringtone_);
        settings.setValue("microphone", microphone_);
        settings.setValue("camera", camera_);
        settings.setValue("camera_resolution", cameraResolution_);
        settings.setValue("camera_frame_rate", cameraFrameRate_);
        settings.setValue("screen_share_frame_rate", screenShareFrameRate_);
        settings.setValue("screen_share_pip", screenSharePiP_);
        settings.setValue("screen_share_remote_video", screenShareRemoteVideo_);
        settings.setValue("screen_share_hide_cursor", screenShareHideCursor_);
        settings.setValue("use_stun_server", useStunServer_);
        settings.setValue("currentProfile", profile_);
        settings.setValue("use_identicon", useIdenticon_);

        settings.endGroup(); // user

        QString prefix =
          (profile_ != "" && profile_ != "default") ? "profile/" + profile_ + "/" : "";
        settings.setValue(prefix + "auth/access_token", accessToken_);
        settings.setValue(prefix + "auth/home_server", homeserver_);
        settings.setValue(prefix + "auth/user_id", userId_);
        settings.setValue(prefix + "auth/device_id", deviceId_);

        settings.sync();
}

HorizontalLine::HorizontalLine(QWidget *parent)
  : QFrame{parent}
{
        setFrameShape(QFrame::HLine);
        setFrameShadow(QFrame::Sunken);
}

UserSettingsPage::UserSettingsPage(QSharedPointer<UserSettings> settings, QWidget *parent)
  : QWidget{parent}
  , settings_{settings}
{
        topLayout_ = new QVBoxLayout{this};

        QIcon icon;
        icon.addFile(":/icons/icons/ui/angle-pointing-to-left.png");

        auto backBtn_ = new FlatButton{this};
        backBtn_->setMinimumSize(QSize(24, 24));
        backBtn_->setIcon(icon);
        backBtn_->setIconSize(QSize(24, 24));

        QFont font;
        font.setPointSizeF(font.pointSizeF() * 1.1);

        auto versionInfo = new QLabel(QString("%1 | %2").arg(nheko::version).arg(nheko::build_os));
        if (QCoreApplication::applicationName() != "nheko")
                versionInfo->setText(versionInfo->text() + " | " +
                                     tr("profile: %1").arg(QCoreApplication::applicationName()));
        versionInfo->setTextInteractionFlags(Qt::TextBrowserInteraction);

        topBarLayout_ = new QHBoxLayout;
        topBarLayout_->setSpacing(0);
        topBarLayout_->setMargin(0);
        topBarLayout_->addWidget(backBtn_, 1, Qt::AlignLeft | Qt::AlignVCenter);
        topBarLayout_->addStretch(1);

        formLayout_ = new QFormLayout;

        formLayout_->setLabelAlignment(Qt::AlignLeft);
        formLayout_->setFormAlignment(Qt::AlignRight);
        formLayout_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        formLayout_->setRowWrapPolicy(QFormLayout::WrapLongRows);
        formLayout_->setHorizontalSpacing(0);

        auto general_ = new QLabel{tr("GENERAL"), this};
        general_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        general_->setFont(font);

        trayToggle_                = new Toggle{this};
        startInTrayToggle_         = new Toggle{this};
        avatarCircles_             = new Toggle{this};
        useIdenticon_              = new Toggle{this};
        decryptSidebar_            = new Toggle(this);
        privacyScreen_             = new Toggle{this};
        shareKeysWithTrustedUsers_ = new Toggle(this);
        groupViewToggle_           = new Toggle{this};
        timelineButtonsToggle_     = new Toggle{this};
        typingNotifications_       = new Toggle{this};
        messageHoverHighlight_     = new Toggle{this};
        enlargeEmojiOnlyMessages_  = new Toggle{this};
        sortByImportance_          = new Toggle{this};
        readReceipts_              = new Toggle{this};
        markdown_                  = new Toggle{this};
        desktopNotifications_      = new Toggle{this};
        alertOnNotification_       = new Toggle{this};
        useStunServer_             = new Toggle{this};
        mobileMode_                = new Toggle{this};
        scaleFactorCombo_          = new QComboBox{this};
        fontSizeCombo_             = new QComboBox{this};
        fontSelectionCombo_        = new QFontComboBox{this};
        emojiFontSelectionCombo_   = new QComboBox{this};
        ringtoneCombo_             = new QComboBox{this};
        microphoneCombo_           = new QComboBox{this};
        cameraCombo_               = new QComboBox{this};
        cameraResolutionCombo_     = new QComboBox{this};
        cameraFrameRateCombo_      = new QComboBox{this};
        timelineMaxWidthSpin_      = new QSpinBox{this};
        privacyScreenTimeout_      = new QSpinBox{this};

        trayToggle_->setChecked(settings_->tray());
        startInTrayToggle_->setChecked(settings_->startInTray());
        avatarCircles_->setChecked(settings_->avatarCircles());
        useIdenticon_->setChecked(settings_->useIdenticon());
        decryptSidebar_->setChecked(settings_->decryptSidebar());
        privacyScreen_->setChecked(settings_->privacyScreen());
        shareKeysWithTrustedUsers_->setChecked(settings_->shareKeysWithTrustedUsers());
        groupViewToggle_->setChecked(settings_->groupView());
        timelineButtonsToggle_->setChecked(settings_->buttonsInTimeline());
        typingNotifications_->setChecked(settings_->typingNotifications());
        messageHoverHighlight_->setChecked(settings_->messageHoverHighlight());
        enlargeEmojiOnlyMessages_->setChecked(settings_->enlargeEmojiOnlyMessages());
        sortByImportance_->setChecked(settings_->sortByImportance());
        readReceipts_->setChecked(settings_->readReceipts());
        markdown_->setChecked(settings_->markdown());
        desktopNotifications_->setChecked(settings_->hasDesktopNotifications());
        alertOnNotification_->setChecked(settings_->hasAlertOnNotification());
        useStunServer_->setChecked(settings_->useStunServer());
        mobileMode_->setChecked(settings_->mobileMode());

        if (!settings_->tray()) {
                startInTrayToggle_->setState(false);
                startInTrayToggle_->setDisabled(true);
        }

        if (!settings_->privacyScreen()) {
                privacyScreenTimeout_->setDisabled(true);
        }

        avatarCircles_->setFixedSize(64, 48);

        auto uiLabel_ = new QLabel{tr("INTERFACE"), this};
        uiLabel_->setFixedHeight(uiLabel_->minimumHeight() + LayoutTopMargin);
        uiLabel_->setAlignment(Qt::AlignBottom);
        uiLabel_->setFont(font);

        for (double option = 1; option <= 3; option += 0.25)
                scaleFactorCombo_->addItem(QString::number(option));
        for (double option = 6; option <= 24; option += 0.5)
                fontSizeCombo_->addItem(QString("%1 ").arg(QString::number(option)));

        QFontDatabase fontDb;

        // TODO: Is there a way to limit to just emojis, rather than
        // all emoji fonts?
        auto emojiFamilies = fontDb.families(QFontDatabase::Symbol);
        emojiFontSelectionCombo_->addItem(tr("Default"));
        for (const auto &family : emojiFamilies) {
                emojiFontSelectionCombo_->addItem(family);
        }

        QString currentFont = settings_->font();
        if (currentFont != "default" || currentFont != "") {
                fontSelectionCombo_->setCurrentIndex(fontSelectionCombo_->findText(currentFont));
        }

        emojiFontSelectionCombo_->setCurrentIndex(
          emojiFontSelectionCombo_->findText(settings_->emojiFont()));

        themeCombo_ = new QComboBox{this};
        themeCombo_->addItem("Light");
        themeCombo_->addItem("Dark");
        themeCombo_->addItem("System");

        QString themeStr = settings_->theme();
        themeStr.replace(0, 1, themeStr[0].toUpper());
        int themeIndex = themeCombo_->findText(themeStr);
        themeCombo_->setCurrentIndex(themeIndex);

        timelineMaxWidthSpin_->setMinimum(0);
        timelineMaxWidthSpin_->setMaximum(100'000'000);
        timelineMaxWidthSpin_->setSingleStep(10);

        privacyScreenTimeout_->setMinimum(0);
        privacyScreenTimeout_->setMaximum(3600);
        privacyScreenTimeout_->setSingleStep(10);

        auto callsLabel = new QLabel{tr("CALLS"), this};
        callsLabel->setFixedHeight(callsLabel->minimumHeight() + LayoutTopMargin);
        callsLabel->setAlignment(Qt::AlignBottom);
        callsLabel->setFont(font);

        auto encryptionLabel_ = new QLabel{tr("ENCRYPTION"), this};
        encryptionLabel_->setFixedHeight(encryptionLabel_->minimumHeight() + LayoutTopMargin);
        encryptionLabel_->setAlignment(Qt::AlignBottom);
        encryptionLabel_->setFont(font);

        QFont monospaceFont;
        monospaceFont.setFamily("Monospace");
        monospaceFont.setStyleHint(QFont::Monospace);
        monospaceFont.setPointSizeF(monospaceFont.pointSizeF() * 0.9);

        deviceIdValue_ = new QLabel{this};
        deviceIdValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        deviceIdValue_->setFont(monospaceFont);

        deviceFingerprintValue_ = new QLabel{this};
        deviceFingerprintValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        deviceFingerprintValue_->setFont(monospaceFont);

        deviceFingerprintValue_->setText(utils::humanReadableFingerprint(QString(44, 'X')));

        backupSecretCached      = new QLabel{this};
        masterSecretCached      = new QLabel{this};
        selfSigningSecretCached = new QLabel{this};
        userSigningSecretCached = new QLabel{this};
        backupSecretCached->setFont(monospaceFont);
        masterSecretCached->setFont(monospaceFont);
        selfSigningSecretCached->setFont(monospaceFont);
        userSigningSecretCached->setFont(monospaceFont);

        auto sessionKeysLabel = new QLabel{tr("Session Keys"), this};
        sessionKeysLabel->setFont(font);
        sessionKeysLabel->setMargin(OptionMargin);

        auto sessionKeysImportBtn = new QPushButton{tr("IMPORT"), this};
        auto sessionKeysExportBtn = new QPushButton{tr("EXPORT"), this};

        auto sessionKeysLayout = new QHBoxLayout;
        sessionKeysLayout->addWidget(new QLabel{"", this}, 1, Qt::AlignRight);
        sessionKeysLayout->addWidget(sessionKeysExportBtn, 0, Qt::AlignRight);
        sessionKeysLayout->addWidget(sessionKeysImportBtn, 0, Qt::AlignRight);

        auto crossSigningKeysLabel = new QLabel{tr("Cross Signing Keys"), this};
        crossSigningKeysLabel->setFont(font);
        crossSigningKeysLabel->setMargin(OptionMargin);

        auto crossSigningRequestBtn  = new QPushButton{tr("REQUEST"), this};
        auto crossSigningDownloadBtn = new QPushButton{tr("DOWNLOAD"), this};

        auto crossSigningKeysLayout = new QHBoxLayout;
        crossSigningKeysLayout->addWidget(new QLabel{"", this}, 1, Qt::AlignRight);
        crossSigningKeysLayout->addWidget(crossSigningRequestBtn, 0, Qt::AlignRight);
        crossSigningKeysLayout->addWidget(crossSigningDownloadBtn, 0, Qt::AlignRight);

        auto boxWrap = [this, &font](QString labelText, QWidget *field, QString tooltipText = "") {
                auto label = new QLabel{labelText, this};
                label->setFont(font);
                label->setMargin(OptionMargin);

                if (!tooltipText.isEmpty()) {
                        label->setToolTip(tooltipText);
                }

                auto layout = new QHBoxLayout;
                layout->addWidget(field, 0, Qt::AlignRight);

                formLayout_->addRow(label, layout);
        };

        formLayout_->addRow(general_);
        formLayout_->addRow(new HorizontalLine{this});
        boxWrap(
          tr("Minimize to tray"),
          trayToggle_,
          tr("Keep the application running in the background after closing the client window."));
        boxWrap(tr("Start in tray"),
                startInTrayToggle_,
                tr("Start the application in the background without showing the client window."));
        formLayout_->addRow(new HorizontalLine{this});
        boxWrap(tr("Circular Avatars"),
                avatarCircles_,
                tr("Change the appearance of user avatars in chats.\nOFF - square, ON - Circle."));
        if (JdenticonProvider::isAvailable())
                boxWrap(
                  tr("Use identicons"),
                  useIdenticon_,
                  tr(
                    "Display an identicon instead of a letter when a user has not set an avatar."));
        boxWrap(tr("Group's sidebar"),
                groupViewToggle_,
                tr("Show a column containing groups and tags next to the room list."));
        boxWrap(tr("Decrypt messages in sidebar"),
                decryptSidebar_,
                tr("Decrypt the messages shown in the sidebar.\nOnly affects messages in "
                   "encrypted chats."));
        boxWrap(tr("Privacy Screen"),
                privacyScreen_,
                tr("When the window loses focus, the timeline will\nbe blurred."));
        boxWrap(
          tr("Privacy screen timeout (in seconds [0 - 3600])"),
          privacyScreenTimeout_,
          tr("Set timeout (in seconds) for how long after window loses\nfocus before the screen"
             " will be blurred.\nSet to 0 to blur immediately after focus loss. Max value of 1 "
             "hour (3600 seconds)"));
        boxWrap(tr("Show buttons in timeline"),
                timelineButtonsToggle_,
                tr("Show buttons to quickly reply, react or access additional options next to each "
                   "message."));
        boxWrap(tr("Limit width of timeline"),
                timelineMaxWidthSpin_,
                tr("Set the max width of messages in the timeline (in pixels). This can help "
                   "readability on wide screen, when Nheko is maximised"));
        boxWrap(tr("Typing notifications"),
                typingNotifications_,
                tr("Show who is typing in a room.\nThis will also enable or disable sending typing "
                   "notifications to others."));
        boxWrap(
          tr("Sort rooms by unreads"),
          sortByImportance_,
          tr(
            "Display rooms with new messages first.\nIf this is off, the list of rooms will only "
            "be sorted by the timestamp of the last message in a room.\nIf this is on, rooms which "
            "have active notifications (the small circle with a number in it) will be sorted on "
            "top. Rooms, that you have muted, will still be sorted by timestamp, since you don't "
            "seem to consider them as important as the other rooms."));
        formLayout_->addRow(new HorizontalLine{this});
        boxWrap(tr("Read receipts"),
                readReceipts_,
                tr("Show if your message was read.\nStatus is displayed next to timestamps."));
        boxWrap(
          tr("Send messages as Markdown"),
          markdown_,
          tr("Allow using markdown in messages.\nWhen disabled, all messages are sent as a plain "
             "text."));
        boxWrap(tr("Desktop notifications"),
                desktopNotifications_,
                tr("Notify about received message when the client is not currently focused."));
        boxWrap(tr("Alert on notification"),
                alertOnNotification_,
                tr("Show an alert when a message is received.\nThis usually causes the application "
                   "icon in the task bar to animate in some fashion."));
        boxWrap(tr("Highlight message on hover"),
                messageHoverHighlight_,
                tr("Change the background color of messages when you hover over them."));
        boxWrap(tr("Large Emoji in timeline"),
                enlargeEmojiOnlyMessages_,
                tr("Make font size larger if messages with only a few emojis are displayed."));
        formLayout_->addRow(uiLabel_);
        formLayout_->addRow(new HorizontalLine{this});

        boxWrap(tr("Touchscreen mode"),
                mobileMode_,
                tr("Will prevent text selection in the timeline to make touch scrolling easier."));
#if !defined(Q_OS_MAC)
        boxWrap(tr("Scale factor"),
                scaleFactorCombo_,
                tr("Change the scale factor of the whole user interface."));
#else
        scaleFactorCombo_->hide();
#endif
        boxWrap(tr("Font size"), fontSizeCombo_);
        boxWrap(tr("Font Family"), fontSelectionCombo_);

#if !defined(Q_OS_MAC)
        boxWrap(tr("Emoji Font Family"), emojiFontSelectionCombo_);
#else
        emojiFontSelectionCombo_->hide();
#endif

        boxWrap(tr("Theme"), themeCombo_);

        formLayout_->addRow(callsLabel);
        formLayout_->addRow(new HorizontalLine{this});
        boxWrap(tr("Ringtone"),
                ringtoneCombo_,
                tr("Set the notification sound to play when a call invite arrives"));
        boxWrap(tr("Microphone"), microphoneCombo_);
        boxWrap(tr("Camera"), cameraCombo_);
        boxWrap(tr("Camera resolution"), cameraResolutionCombo_);
        boxWrap(tr("Camera frame rate"), cameraFrameRateCombo_);

        ringtoneCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        ringtoneCombo_->addItem("Mute");
        ringtoneCombo_->addItem("Default");
        ringtoneCombo_->addItem("Other...");
        const QString &ringtone = settings_->ringtone();
        if (!ringtone.isEmpty() && ringtone != "Mute" && ringtone != "Default")
                ringtoneCombo_->addItem(ringtone);
        microphoneCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        cameraCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        cameraResolutionCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        cameraFrameRateCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        boxWrap(tr("Allow fallback call assist server"),
                useStunServer_,
                tr("Will use turn.matrix.org as assist when your home server does not offer one."));

        formLayout_->addRow(encryptionLabel_);
        formLayout_->addRow(new HorizontalLine{this});
        boxWrap(tr("Device ID"), deviceIdValue_);
        boxWrap(tr("Device Fingerprint"), deviceFingerprintValue_);
        boxWrap(
          tr("Share keys with verified users and devices"),
          shareKeysWithTrustedUsers_,
          tr("Automatically replies to key requests from other users, if they are verified."));
        formLayout_->addRow(new HorizontalLine{this});
        formLayout_->addRow(sessionKeysLabel, sessionKeysLayout);
        formLayout_->addRow(crossSigningKeysLabel, crossSigningKeysLayout);

        boxWrap(tr("Master signing key"),
                masterSecretCached,
                tr("Your most important key. You don't need to have it cached, since not caching "
                   "it makes it less likely it can be stolen and it is only needed to rotate your "
                   "other signing keys."));
        boxWrap(tr("User signing key"),
                userSigningSecretCached,
                tr("The key to verify other users. If it is cached, verifying a user will verify "
                   "all their devices."));
        boxWrap(
          tr("Self signing key"),
          selfSigningSecretCached,
          tr("The key to verify your own devices. If it is cached, verifying one of your devices "
             "will mark it verified for all your other devices and for users, that have verified "
             "you."));
        boxWrap(tr("Backup key"),
                backupSecretCached,
                tr("The key to decrypt online key backups. If it is cached, you can enable online "
                   "key backup to store encryption keys securely encrypted on the server."));
        updateSecretStatus();

        auto scrollArea_ = new QScrollArea{this};
        scrollArea_->setFrameShape(QFrame::NoFrame);
        scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        scrollArea_->setWidgetResizable(true);
        scrollArea_->setAlignment(Qt::AlignTop | Qt::AlignVCenter);

        QScroller::grabGesture(scrollArea_, QScroller::TouchGesture);

        auto spacingAroundForm = new QHBoxLayout;
        spacingAroundForm->addStretch(1);
        spacingAroundForm->addLayout(formLayout_, 0);
        spacingAroundForm->addStretch(1);

        auto scrollAreaContents_ = new QWidget{this};
        scrollAreaContents_->setObjectName("UserSettingScrollWidget");
        scrollAreaContents_->setLayout(spacingAroundForm);

        scrollArea_->setWidget(scrollAreaContents_);
        topLayout_->addLayout(topBarLayout_);
        topLayout_->addWidget(scrollArea_, Qt::AlignTop);
        topLayout_->addStretch(1);
        topLayout_->addWidget(versionInfo);

        connect(themeCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &text) {
                        settings_->setTheme(text.toLower());
                        emit themeChanged();
                });
        connect(scaleFactorCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [](const QString &factor) { utils::setScaleFactor(factor.toFloat()); });
        connect(fontSizeCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &size) { settings_->setFontSize(size.trimmed().toDouble()); });
        connect(fontSelectionCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &family) { settings_->setFontFamily(family.trimmed()); });
        connect(emojiFontSelectionCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &family) { settings_->setEmojiFontFamily(family.trimmed()); });

        connect(ringtoneCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &ringtone) {
                        if (ringtone == "Other...") {
                                QString homeFolder =
                                  QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
                                auto filepath = QFileDialog::getOpenFileName(
                                  this, tr("Select a file"), homeFolder, tr("All Files (*)"));
                                if (!filepath.isEmpty()) {
                                        const auto &oldSetting = settings_->ringtone();
                                        if (oldSetting != "Mute" && oldSetting != "Default")
                                                ringtoneCombo_->removeItem(
                                                  ringtoneCombo_->findText(oldSetting));
                                        settings_->setRingtone(filepath);
                                        ringtoneCombo_->addItem(filepath);
                                        ringtoneCombo_->setCurrentText(filepath);
                                } else {
                                        ringtoneCombo_->setCurrentText(settings_->ringtone());
                                }
                        } else if (ringtone == "Mute" || ringtone == "Default") {
                                const auto &oldSetting = settings_->ringtone();
                                if (oldSetting != "Mute" && oldSetting != "Default")
                                        ringtoneCombo_->removeItem(
                                          ringtoneCombo_->findText(oldSetting));
                                settings_->setRingtone(ringtone);
                        }
                });

        connect(microphoneCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &microphone) { settings_->setMicrophone(microphone); });

        connect(cameraCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &camera) {
                        settings_->setCamera(camera);
                        std::vector<std::string> resolutions =
                          CallDevices::instance().resolutions(camera.toStdString());
                        cameraResolutionCombo_->clear();
                        for (const auto &resolution : resolutions)
                                cameraResolutionCombo_->addItem(QString::fromStdString(resolution));
                });

        connect(cameraResolutionCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &resolution) {
                        settings_->setCameraResolution(resolution);
                        std::vector<std::string> frameRates = CallDevices::instance().frameRates(
                          settings_->camera().toStdString(), resolution.toStdString());
                        cameraFrameRateCombo_->clear();
                        for (const auto &frameRate : frameRates)
                                cameraFrameRateCombo_->addItem(QString::fromStdString(frameRate));
                });

        connect(cameraFrameRateCombo_,
                static_cast<void (QComboBox::*)(const QString &)>(&QComboBox::currentTextChanged),
                [this](const QString &frameRate) { settings_->setCameraFrameRate(frameRate); });

        connect(trayToggle_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setTray(enabled);
                if (enabled) {
                        startInTrayToggle_->setChecked(false);
                        startInTrayToggle_->setEnabled(true);
                        startInTrayToggle_->setState(false);
                        settings_->setStartInTray(false);
                } else {
                        startInTrayToggle_->setChecked(false);
                        startInTrayToggle_->setState(false);
                        startInTrayToggle_->setDisabled(true);
                        settings_->setStartInTray(false);
                }
                emit trayOptionChanged(enabled);
        });

        connect(startInTrayToggle_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setStartInTray(enabled);
        });

        connect(mobileMode_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setMobileMode(enabled);
        });

        connect(groupViewToggle_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setGroupView(enabled);
        });

        connect(decryptSidebar_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setDecryptSidebar(enabled);
        });

        connect(privacyScreen_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setPrivacyScreen(enabled);
                if (enabled) {
                        privacyScreenTimeout_->setEnabled(true);
                } else {
                        privacyScreenTimeout_->setDisabled(true);
                }
        });

        connect(shareKeysWithTrustedUsers_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setShareKeysWithTrustedUsers(enabled);
        });

        connect(avatarCircles_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setAvatarCircles(enabled);
        });

        connect(useIdenticon_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setUseIdenticon(enabled);
        });

        connect(markdown_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setMarkdown(enabled);
        });

        connect(typingNotifications_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setTypingNotifications(enabled);
        });

        connect(sortByImportance_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setSortByImportance(enabled);
        });

        connect(timelineButtonsToggle_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setButtonsInTimeline(enabled);
        });

        connect(readReceipts_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setReadReceipts(enabled);
        });

        connect(desktopNotifications_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setDesktopNotifications(enabled);
        });

        connect(alertOnNotification_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setAlertOnNotification(enabled);
        });

        connect(messageHoverHighlight_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setMessageHoverHighlight(enabled);
        });

        connect(enlargeEmojiOnlyMessages_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setEnlargeEmojiOnlyMessages(enabled);
        });

        connect(useStunServer_, &Toggle::toggled, this, [this](bool enabled) {
                settings_->setUseStunServer(enabled);
        });

        connect(timelineMaxWidthSpin_,
                qOverload<int>(&QSpinBox::valueChanged),
                this,
                [this](int newValue) { settings_->setTimelineMaxWidth(newValue); });

        connect(privacyScreenTimeout_,
                qOverload<int>(&QSpinBox::valueChanged),
                this,
                [this](int newValue) { settings_->setPrivacyScreenTimeout(newValue); });

        connect(
          sessionKeysImportBtn, &QPushButton::clicked, this, &UserSettingsPage::importSessionKeys);

        connect(
          sessionKeysExportBtn, &QPushButton::clicked, this, &UserSettingsPage::exportSessionKeys);

        connect(crossSigningRequestBtn, &QPushButton::clicked, this, []() {
                olm::request_cross_signing_keys();
        });

        connect(crossSigningDownloadBtn, &QPushButton::clicked, this, []() {
                olm::download_cross_signing_keys();
        });

        connect(backBtn_, &QPushButton::clicked, this, [this]() {
                settings_->save();
                emit moveBack();
        });
}

void
UserSettingsPage::showEvent(QShowEvent *)
{
        // FIXME macOS doesn't show the full option unless a space is added.
        utils::restoreCombobox(fontSizeCombo_, QString::number(settings_->fontSize()) + " ");
        utils::restoreCombobox(scaleFactorCombo_, QString::number(utils::scaleFactor()));
        utils::restoreCombobox(themeCombo_, settings_->theme());
        utils::restoreCombobox(ringtoneCombo_, settings_->ringtone());

        trayToggle_->setState(settings_->tray());
        startInTrayToggle_->setState(settings_->startInTray());
        groupViewToggle_->setState(settings_->groupView());
        decryptSidebar_->setState(settings_->decryptSidebar());
        privacyScreen_->setState(settings_->privacyScreen());
        shareKeysWithTrustedUsers_->setState(settings_->shareKeysWithTrustedUsers());
        avatarCircles_->setState(settings_->avatarCircles());
        typingNotifications_->setState(settings_->typingNotifications());
        sortByImportance_->setState(settings_->sortByImportance());
        timelineButtonsToggle_->setState(settings_->buttonsInTimeline());
        mobileMode_->setState(settings_->mobileMode());
        readReceipts_->setState(settings_->readReceipts());
        markdown_->setState(settings_->markdown());
        desktopNotifications_->setState(settings_->hasDesktopNotifications());
        alertOnNotification_->setState(settings_->hasAlertOnNotification());
        messageHoverHighlight_->setState(settings_->messageHoverHighlight());
        enlargeEmojiOnlyMessages_->setState(settings_->enlargeEmojiOnlyMessages());
        deviceIdValue_->setText(QString::fromStdString(http::client()->device_id()));
        timelineMaxWidthSpin_->setValue(settings_->timelineMaxWidth());
        privacyScreenTimeout_->setValue(settings_->privacyScreenTimeout());

        auto mics = CallDevices::instance().names(false, settings_->microphone().toStdString());
        microphoneCombo_->clear();
        for (const auto &m : mics)
                microphoneCombo_->addItem(QString::fromStdString(m));

        auto cameraResolution = settings_->cameraResolution();
        auto cameraFrameRate  = settings_->cameraFrameRate();

        auto cameras = CallDevices::instance().names(true, settings_->camera().toStdString());
        cameraCombo_->clear();
        for (const auto &c : cameras)
                cameraCombo_->addItem(QString::fromStdString(c));

        utils::restoreCombobox(cameraResolutionCombo_, cameraResolution);
        utils::restoreCombobox(cameraFrameRateCombo_, cameraFrameRate);

        useStunServer_->setState(settings_->useStunServer());

        deviceFingerprintValue_->setText(
          utils::humanReadableFingerprint(olm::client()->identity_keys().ed25519));
}

void
UserSettingsPage::paintEvent(QPaintEvent *)
{
        QStyleOption opt;
        opt.init(this);
        QPainter p(this);
        style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void
UserSettingsPage::importSessionKeys()
{
        const QString homeFolder = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        const QString fileName =
          QFileDialog::getOpenFileName(this, tr("Open Sessions File"), homeFolder, "");

        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, tr("Error"), file.errorString());
                return;
        }

        auto bin     = file.peek(file.size());
        auto payload = std::string(bin.data(), bin.size());

        bool ok;
        auto password = QInputDialog::getText(this,
                                              tr("File Password"),
                                              tr("Enter the passphrase to decrypt the file:"),
                                              QLineEdit::Password,
                                              "",
                                              &ok);
        if (!ok)
                return;

        if (password.isEmpty()) {
                QMessageBox::warning(this, tr("Error"), tr("The password cannot be empty"));
                return;
        }

        try {
                auto sessions =
                  mtx::crypto::decrypt_exported_sessions(payload, password.toStdString());
                cache::importSessionKeys(std::move(sessions));
        } catch (const std::exception &e) {
                QMessageBox::warning(this, tr("Error"), e.what());
        }
}

void
UserSettingsPage::exportSessionKeys()
{
        // Open password dialog.
        bool ok;
        auto password = QInputDialog::getText(this,
                                              tr("File Password"),
                                              tr("Enter passphrase to encrypt your session keys:"),
                                              QLineEdit::Password,
                                              "",
                                              &ok);
        if (!ok)
                return;

        if (password.isEmpty()) {
                QMessageBox::warning(this, tr("Error"), tr("The password cannot be empty"));
                return;
        }

        // Open file dialog to save the file.
        const QString homeFolder = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        const QString fileName =
          QFileDialog::getSaveFileName(this, tr("File to save the exported session keys"), "", "");

        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QMessageBox::warning(this, tr("Error"), file.errorString());
                return;
        }

        // Export sessions & save to file.
        try {
                auto encrypted_blob = mtx::crypto::encrypt_exported_sessions(
                  cache::exportSessionKeys(), password.toStdString());

                QString b64 = QString::fromStdString(mtx::crypto::bin2base64(encrypted_blob));

                QString prefix("-----BEGIN MEGOLM SESSION DATA-----");
                QString suffix("-----END MEGOLM SESSION DATA-----");
                QString newline("\n");
                QTextStream out(&file);
                out << prefix << newline << b64 << newline << suffix;
                file.close();
        } catch (const std::exception &e) {
                QMessageBox::warning(this, tr("Error"), e.what());
        }
}

void
UserSettingsPage::updateSecretStatus()
{
        QString ok      = "QLabel { color : #00cc66; }";
        QString notSoOk = "QLabel { color : #ff9933; }";

        auto updateLabel = [&ok, &notSoOk](QLabel *label, const std::string &secretName) {
                if (cache::secret(secretName)) {
                        label->setStyleSheet(ok);
                        label->setText(tr("CACHED"));
                } else {
                        if (secretName == mtx::secret_storage::secrets::cross_signing_master)
                                label->setStyleSheet(ok);
                        else
                                label->setStyleSheet(notSoOk);
                        label->setText(tr("NOT CACHED"));
                }
        };

        updateLabel(masterSecretCached, mtx::secret_storage::secrets::cross_signing_master);
        updateLabel(userSigningSecretCached,
                    mtx::secret_storage::secrets::cross_signing_user_signing);
        updateLabel(selfSigningSecretCached,
                    mtx::secret_storage::secrets::cross_signing_self_signing);
        updateLabel(backupSecretCached, mtx::secret_storage::secrets::megolm_backup_v1);
}
