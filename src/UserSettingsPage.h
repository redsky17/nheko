// SPDX-FileCopyrightText: 2017 Konstantinos Sideris <siderisk@auth.gr>
// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFontDatabase>
#include <QFrame>
#include <QProcessEnvironment>
#include <QSharedPointer>
#include <QWidget>

#include <optional>

class Toggle;
class QLabel;
class QFormLayout;
class QComboBox;
class QFontComboBox;
class QSpinBox;
class QHBoxLayout;
class QVBoxLayout;

constexpr int OptionMargin       = 6;
constexpr int LayoutTopMargin    = 50;
constexpr int LayoutBottomMargin = LayoutTopMargin;

class UserSettings : public QObject
{
        Q_OBJECT

        Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
        Q_PROPERTY(bool messageHoverHighlight READ messageHoverHighlight WRITE
                     setMessageHoverHighlight NOTIFY messageHoverHighlightChanged)
        Q_PROPERTY(bool enlargeEmojiOnlyMessages READ enlargeEmojiOnlyMessages WRITE
                     setEnlargeEmojiOnlyMessages NOTIFY enlargeEmojiOnlyMessagesChanged)
        Q_PROPERTY(bool tray READ tray WRITE setTray NOTIFY trayChanged)
        Q_PROPERTY(bool startInTray READ startInTray WRITE setStartInTray NOTIFY startInTrayChanged)
        Q_PROPERTY(bool groupView READ groupView WRITE setGroupView NOTIFY groupViewStateChanged)
        Q_PROPERTY(bool markdown READ markdown WRITE setMarkdown NOTIFY markdownChanged)
        Q_PROPERTY(bool typingNotifications READ typingNotifications WRITE setTypingNotifications
                     NOTIFY typingNotificationsChanged)
        Q_PROPERTY(bool sortByImportance READ sortByImportance WRITE setSortByImportance NOTIFY
                     roomSortingChanged)
        Q_PROPERTY(bool buttonsInTimeline READ buttonsInTimeline WRITE setButtonsInTimeline NOTIFY
                     buttonInTimelineChanged)
        Q_PROPERTY(
          bool readReceipts READ readReceipts WRITE setReadReceipts NOTIFY readReceiptsChanged)
        Q_PROPERTY(bool desktopNotifications READ hasDesktopNotifications WRITE
                     setDesktopNotifications NOTIFY desktopNotificationsChanged)
        Q_PROPERTY(bool alertOnNotification READ hasAlertOnNotification WRITE setAlertOnNotification
                     NOTIFY alertOnNotificationChanged)
        Q_PROPERTY(
          bool avatarCircles READ avatarCircles WRITE setAvatarCircles NOTIFY avatarCirclesChanged)
        Q_PROPERTY(bool decryptSidebar READ decryptSidebar WRITE setDecryptSidebar NOTIFY
                     decryptSidebarChanged)
        Q_PROPERTY(
          bool privacyScreen READ privacyScreen WRITE setPrivacyScreen NOTIFY privacyScreenChanged)
        Q_PROPERTY(int privacyScreenTimeout READ privacyScreenTimeout WRITE setPrivacyScreenTimeout
                     NOTIFY privacyScreenTimeoutChanged)
        Q_PROPERTY(int timelineMaxWidth READ timelineMaxWidth WRITE setTimelineMaxWidth NOTIFY
                     timelineMaxWidthChanged)
        Q_PROPERTY(
          int roomListWidth READ roomListWidth WRITE setRoomListWidth NOTIFY roomListWidthChanged)
        Q_PROPERTY(int communityListWidth READ communityListWidth WRITE setCommunityListWidth NOTIFY
                     communityListWidthChanged)
        Q_PROPERTY(bool mobileMode READ mobileMode WRITE setMobileMode NOTIFY mobileModeChanged)
        Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
        Q_PROPERTY(QString font READ font WRITE setFontFamily NOTIFY fontChanged)
        Q_PROPERTY(
          QString emojiFont READ emojiFont WRITE setEmojiFontFamily NOTIFY emojiFontChanged)
        Q_PROPERTY(Presence presence READ presence WRITE setPresence NOTIFY presenceChanged)
        Q_PROPERTY(QString ringtone READ ringtone WRITE setRingtone NOTIFY ringtoneChanged)
        Q_PROPERTY(QString microphone READ microphone WRITE setMicrophone NOTIFY microphoneChanged)
        Q_PROPERTY(QString camera READ camera WRITE setCamera NOTIFY cameraChanged)
        Q_PROPERTY(QString cameraResolution READ cameraResolution WRITE setCameraResolution NOTIFY
                     cameraResolutionChanged)
        Q_PROPERTY(QString cameraFrameRate READ cameraFrameRate WRITE setCameraFrameRate NOTIFY
                     cameraFrameRateChanged)
        Q_PROPERTY(int screenShareFrameRate READ screenShareFrameRate WRITE setScreenShareFrameRate
                     NOTIFY screenShareFrameRateChanged)
        Q_PROPERTY(bool screenSharePiP READ screenSharePiP WRITE setScreenSharePiP NOTIFY
                     screenSharePiPChanged)
        Q_PROPERTY(bool screenShareRemoteVideo READ screenShareRemoteVideo WRITE
                     setScreenShareRemoteVideo NOTIFY screenShareRemoteVideoChanged)
        Q_PROPERTY(bool screenShareHideCursor READ screenShareHideCursor WRITE
                     setScreenShareHideCursor NOTIFY screenShareHideCursorChanged)
        Q_PROPERTY(
          bool useStunServer READ useStunServer WRITE setUseStunServer NOTIFY useStunServerChanged)
        Q_PROPERTY(bool shareKeysWithTrustedUsers READ shareKeysWithTrustedUsers WRITE
                     setShareKeysWithTrustedUsers NOTIFY shareKeysWithTrustedUsersChanged)
        Q_PROPERTY(QString profile READ profile WRITE setProfile NOTIFY profileChanged)
        Q_PROPERTY(QString userId READ userId WRITE setUserId NOTIFY userIdChanged)
        Q_PROPERTY(
          QString accessToken READ accessToken WRITE setAccessToken NOTIFY accessTokenChanged)
        Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY deviceIdChanged)
        Q_PROPERTY(QString homeserver READ homeserver WRITE setHomeserver NOTIFY homeserverChanged)
        Q_PROPERTY(bool disableCertificateValidation READ disableCertificateValidation WRITE
                     setDisableCertificateValidation NOTIFY disableCertificateValidationChanged)

        UserSettings();

public:
        static QSharedPointer<UserSettings> instance();
        static void initialize(std::optional<QString> profile);

        enum class Presence
        {
                AutomaticPresence,
                Online,
                Unavailable,
                Offline,
        };
        Q_ENUM(Presence)

        void save();
        void load(std::optional<QString> profile);
        void applyTheme();
        void setTheme(QString theme);
        void setMessageHoverHighlight(bool state);
        void setEnlargeEmojiOnlyMessages(bool state);
        void setTray(bool state);
        void setStartInTray(bool state);
        void setMobileMode(bool mode);
        void setFontSize(double size);
        void setFontFamily(QString family);
        void setEmojiFontFamily(QString family);
        void setGroupView(bool state);
        void setMarkdown(bool state);
        void setReadReceipts(bool state);
        void setTypingNotifications(bool state);
        void setSortByImportance(bool state);
        void setButtonsInTimeline(bool state);
        void setTimelineMaxWidth(int state);
        void setCommunityListWidth(int state);
        void setRoomListWidth(int state);
        void setDesktopNotifications(bool state);
        void setAlertOnNotification(bool state);
        void setAvatarCircles(bool state);
        void setDecryptSidebar(bool state);
        void setPrivacyScreen(bool state);
        void setPrivacyScreenTimeout(int state);
        void setPresence(Presence state);
        void setRingtone(QString ringtone);
        void setMicrophone(QString microphone);
        void setCamera(QString camera);
        void setCameraResolution(QString resolution);
        void setCameraFrameRate(QString frameRate);
        void setScreenShareFrameRate(int frameRate);
        void setScreenSharePiP(bool state);
        void setScreenShareRemoteVideo(bool state);
        void setScreenShareHideCursor(bool state);
        void setUseStunServer(bool state);
        void setShareKeysWithTrustedUsers(bool state);
        void setProfile(QString profile);
        void setUserId(QString userId);
        void setAccessToken(QString accessToken);
        void setDeviceId(QString deviceId);
        void setHomeserver(QString homeserver);
        void setDisableCertificateValidation(bool disabled);
        void setHiddenTags(QStringList hiddenTags);

        QString theme() const { return !theme_.isEmpty() ? theme_ : defaultTheme_; }
        bool messageHoverHighlight() const { return messageHoverHighlight_; }
        bool enlargeEmojiOnlyMessages() const { return enlargeEmojiOnlyMessages_; }
        bool tray() const { return tray_; }
        bool startInTray() const { return startInTray_; }
        bool groupView() const { return groupView_; }
        bool avatarCircles() const { return avatarCircles_; }
        bool decryptSidebar() const { return decryptSidebar_; }
        bool privacyScreen() const { return privacyScreen_; }
        int privacyScreenTimeout() const { return privacyScreenTimeout_; }
        bool markdown() const { return markdown_; }
        bool typingNotifications() const { return typingNotifications_; }
        bool sortByImportance() const { return sortByImportance_; }
        bool buttonsInTimeline() const { return buttonsInTimeline_; }
        bool mobileMode() const { return mobileMode_; }
        bool readReceipts() const { return readReceipts_; }
        bool hasDesktopNotifications() const { return hasDesktopNotifications_; }
        bool hasAlertOnNotification() const { return hasAlertOnNotification_; }
        bool hasNotifications() const
        {
                return hasDesktopNotifications() || hasAlertOnNotification();
        }
        int timelineMaxWidth() const { return timelineMaxWidth_; }
        int communityListWidth() const { return communityListWidth_; }
        int roomListWidth() const { return roomListWidth_; }
        double fontSize() const { return baseFontSize_; }
        QString font() const { return font_; }
        QString emojiFont() const
        {
                if (emojiFont_ == "Default") {
                        return tr("Default");
                }

                return emojiFont_;
        }
        Presence presence() const { return presence_; }
        QString ringtone() const { return ringtone_; }
        QString microphone() const { return microphone_; }
        QString camera() const { return camera_; }
        QString cameraResolution() const { return cameraResolution_; }
        QString cameraFrameRate() const { return cameraFrameRate_; }
        int screenShareFrameRate() const { return screenShareFrameRate_; }
        bool screenSharePiP() const { return screenSharePiP_; }
        bool screenShareRemoteVideo() const { return screenShareRemoteVideo_; }
        bool screenShareHideCursor() const { return screenShareHideCursor_; }
        bool useStunServer() const { return useStunServer_; }
        bool shareKeysWithTrustedUsers() const { return shareKeysWithTrustedUsers_; }
        QString profile() const { return profile_; }
        QString userId() const { return userId_; }
        QString accessToken() const { return accessToken_; }
        QString deviceId() const { return deviceId_; }
        QString homeserver() const { return homeserver_; }
        bool disableCertificateValidation() const
        {
#ifdef Q_OS_ANDROID
                  return true;
#else
                  return disableCertificateValidation_;
#endif
        }
        QStringList hiddenTags() const { return hiddenTags_; }

signals:
        void groupViewStateChanged(bool state);
        void roomSortingChanged(bool state);
        void themeChanged(QString state);
        void messageHoverHighlightChanged(bool state);
        void enlargeEmojiOnlyMessagesChanged(bool state);
        void trayChanged(bool state);
        void startInTrayChanged(bool state);
        void markdownChanged(bool state);
        void typingNotificationsChanged(bool state);
        void buttonInTimelineChanged(bool state);
        void readReceiptsChanged(bool state);
        void desktopNotificationsChanged(bool state);
        void alertOnNotificationChanged(bool state);
        void avatarCirclesChanged(bool state);
        void decryptSidebarChanged(bool state);
        void privacyScreenChanged(bool state);
        void privacyScreenTimeoutChanged(int state);
        void timelineMaxWidthChanged(int state);
        void roomListWidthChanged(int state);
        void communityListWidthChanged(int state);
        void mobileModeChanged(bool mode);
        void fontSizeChanged(double state);
        void fontChanged(QString state);
        void emojiFontChanged(QString state);
        void presenceChanged(Presence state);
        void ringtoneChanged(QString ringtone);
        void microphoneChanged(QString microphone);
        void cameraChanged(QString camera);
        void cameraResolutionChanged(QString resolution);
        void cameraFrameRateChanged(QString frameRate);
        void screenShareFrameRateChanged(int frameRate);
        void screenSharePiPChanged(bool state);
        void screenShareRemoteVideoChanged(bool state);
        void screenShareHideCursorChanged(bool state);
        void useStunServerChanged(bool state);
        void shareKeysWithTrustedUsersChanged(bool state);
        void profileChanged(QString profile);
        void userIdChanged(QString userId);
        void accessTokenChanged(QString accessToken);
        void deviceIdChanged(QString deviceId);
        void homeserverChanged(QString homeserver);
        void disableCertificateValidationChanged(bool disabled);

private:
        // Default to system theme if QT_QPA_PLATFORMTHEME var is set.
        QString defaultTheme_ =
          QProcessEnvironment::systemEnvironment().value("QT_QPA_PLATFORMTHEME", "").isEmpty()
            ? "light"
            : "system";
        QString theme_;
        bool messageHoverHighlight_;
        bool enlargeEmojiOnlyMessages_;
        bool tray_;
        bool startInTray_;
        bool groupView_;
        bool markdown_;
        bool typingNotifications_;
        bool sortByImportance_;
        bool buttonsInTimeline_;
        bool readReceipts_;
        bool hasDesktopNotifications_;
        bool hasAlertOnNotification_;
        bool avatarCircles_;
        bool decryptSidebar_;
        bool privacyScreen_;
        int privacyScreenTimeout_;
        bool shareKeysWithTrustedUsers_;
        bool mobileMode_;
        int timelineMaxWidth_;
        int roomListWidth_;
        int communityListWidth_;
        double baseFontSize_;
        QString font_;
        QString emojiFont_;
        Presence presence_;
        QString ringtone_;
        QString microphone_;
        QString camera_;
        QString cameraResolution_;
        QString cameraFrameRate_;
        int screenShareFrameRate_;
        bool screenSharePiP_;
        bool screenShareRemoteVideo_;
        bool screenShareHideCursor_;
        bool useStunServer_;
        bool disableCertificateValidation_ = false;
        QString profile_;
        QString userId_;
        QString accessToken_;
        QString deviceId_;
        QString homeserver_;
        QStringList hiddenTags_;

        static QSharedPointer<UserSettings> instance_;
};

class HorizontalLine : public QFrame
{
        Q_OBJECT

public:
        HorizontalLine(QWidget *parent = nullptr);
};

class UserSettingsPage : public QWidget
{
        Q_OBJECT

public:
        UserSettingsPage(QSharedPointer<UserSettings> settings, QWidget *parent = nullptr);

protected:
        void showEvent(QShowEvent *event) override;
        void paintEvent(QPaintEvent *event) override;

signals:
        void moveBack();
        void trayOptionChanged(bool value);
        void themeChanged();
        void decryptSidebarChanged();

public slots:
        void updateSecretStatus();

private slots:
        void importSessionKeys();
        void exportSessionKeys();

private:
        // Layouts
        QVBoxLayout *topLayout_;
        QHBoxLayout *topBarLayout_;
        QFormLayout *formLayout_;

        // Shared settings object.
        QSharedPointer<UserSettings> settings_;

        Toggle *trayToggle_;
        Toggle *startInTrayToggle_;
        Toggle *groupViewToggle_;
        Toggle *timelineButtonsToggle_;
        Toggle *typingNotifications_;
        Toggle *messageHoverHighlight_;
        Toggle *enlargeEmojiOnlyMessages_;
        Toggle *sortByImportance_;
        Toggle *readReceipts_;
        Toggle *markdown_;
        Toggle *desktopNotifications_;
        Toggle *alertOnNotification_;
        Toggle *avatarCircles_;
        Toggle *useStunServer_;
        Toggle *decryptSidebar_;
        Toggle *privacyScreen_;
        QSpinBox *privacyScreenTimeout_;
        Toggle *shareKeysWithTrustedUsers_;
        Toggle *mobileMode_;
        QLabel *deviceFingerprintValue_;
        QLabel *deviceIdValue_;
        QLabel *backupSecretCached;
        QLabel *masterSecretCached;
        QLabel *selfSigningSecretCached;
        QLabel *userSigningSecretCached;

        QComboBox *themeCombo_;
        QComboBox *scaleFactorCombo_;
        QComboBox *fontSizeCombo_;
        QFontComboBox *fontSelectionCombo_;
        QComboBox *emojiFontSelectionCombo_;
        QComboBox *ringtoneCombo_;
        QComboBox *microphoneCombo_;
        QComboBox *cameraCombo_;
        QComboBox *cameraResolutionCombo_;
        QComboBox *cameraFrameRateCombo_;

        QSpinBox *timelineMaxWidthSpin_;

        int sideMargin_ = 0;
};
