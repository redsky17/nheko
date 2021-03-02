import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2
import QtQuick.Window 2.3
import im.nheko 1.0

ApplicationWindow {
    id: roomSettingsDialog

    property var roomSettings

    x: MainWindow.x + (MainWindow.width / 2) - (width / 2)
    y: MainWindow.y + (MainWindow.height / 2) - (height / 2)
    minimumWidth: 420
    minimumHeight: 650
    palette: colors
    color: colors.window
    modality: Qt.WindowModal

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: roomSettingsDialog.close()
    }

    ColumnLayout {
        id: contentLayout1

        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Avatar {
            url: roomSettings.roomAvatarUrl.replace("mxc://", "image://MxcImage/")
            height: 130
            width: 130
            Layout.alignment: Qt.AlignHCenter
            onClicked: {
                if (roomSettings.canChangeAvatar)
                    roomSettings.updateAvatar();

            }
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: roomSettings.isLoading
            visible: roomSettings.isLoading
        }

        Text {
            id: errorText

            text: "Error Text"
            color: "red"
            visible: opacity > 0
            opacity: 0
            Layout.alignment: Qt.AlignHCenter
        }

        SequentialAnimation {
            id: hideErrorAnimation

            running: false

            PauseAnimation {
                duration: 4000
            }

            NumberAnimation {
                target: errorText
                property: 'opacity'
                to: 0
                duration: 1000
            }

        }

        Connections {
            target: roomSettings
            onDisplayError: {
                errorText.text = errorMessage;
                errorText.opacity = 1;
                hideErrorAnimation.restart();
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter

            MatrixText {
                text: roomSettings.roomName
                font.pixelSize: 24
                Layout.alignment: Qt.AlignHCenter
            }

            MatrixText {
                text: "%1 member(s)".arg(roomSettings.memberCount)
                Layout.alignment: Qt.AlignHCenter
            }

        }

        ImageButton {
            Layout.alignment: Qt.AlignHCenter
            image: ":/icons/icons/ui/edit.png"
            visible: roomSettings.canChangeNameAndTopic
            onClicked: roomSettings.openEditModal()
        }

        ScrollView {
            Layout.maximumHeight: 75
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true

            TextArea {
                text: roomSettings.roomTopic
                wrapMode: TextEdit.WordWrap
                readOnly: true
                background: null
                selectByMouse: true
                color: colors.text
                horizontalAlignment: TextEdit.AlignHCenter
            }

        }

        GridLayout {
            columns: 2
            rowSpacing: 10

            MatrixText {
                text: "SETTINGS"
                font.bold: true
            }

            Item {
                Layout.fillWidth: true
            }

            MatrixText {
                text: "Notifications"
                Layout.fillWidth: true
            }

            ComboBox {
                model: ["Muted", "Mentions only", "All messages"]
                currentIndex: roomSettings.notifications
                onActivated: {
                    roomSettings.changeNotifications(index);
                }
                Layout.fillWidth: true
            }

            MatrixText {
                text: "Room access"
                Layout.fillWidth: true
            }

            ComboBox {
                enabled: roomSettings.canChangeJoinRules
                model: ["Anyone and guests", "Anyone", "Invited users"]
                currentIndex: roomSettings.accessJoinRules
                onActivated: {
                    roomSettings.changeAccessRules(index);
                }
                Layout.fillWidth: true
            }

            MatrixText {
                text: "Encryption"
            }

            ToggleButton {
                id: encryptionToggle

                checked: roomSettings.isEncryptionEnabled
                onClicked: {
                    if (roomSettings.isEncryptionEnabled) {
                        checked = true;
                        return ;
                    }
                    confirmEncryptionDialog.open();
                }
                Layout.alignment: Qt.AlignRight
            }

            MessageDialog {
                id: confirmEncryptionDialog

                title: qsTr("End-to-End Encryption")
                text: qsTr("Encryption is currently experimental and things might break unexpectedly. <br>
                            Please take note that it can't be disabled afterwards.")
                modality: Qt.WindowModal
                icon: StandardIcon.Question
                onAccepted: {
                    if (roomSettings.isEncryptionEnabled)
                        return ;

                    roomSettings.enableEncryption();
                }
                onRejected: {
                    encryptionToggle.checked = false;
                }
                standardButtons: Dialog.Ok | Dialog.Cancel
            }

            MatrixText {
                visible: roomSettings.isEncryptionEnabled
                text: "Respond to key requests"
            }

            ToggleButton {
                visible: roomSettings.isEncryptionEnabled
                ToolTip.text: qsTr("Whether or not the client should respond automatically with the session keys
                                upon request. Use with caution, this is a temporary measure to test the
                                E2E implementation until device verification is completed.")
                checked: roomSettings.respondsToKeyRequests
                onClicked: {
                    roomSettings.changeKeyRequestsPreference(checked);
                }
                Layout.alignment: Qt.AlignRight
            }

            Item {
                // for adding extra space between sections
                Layout.fillWidth: true
            }

            Item {
                // for adding extra space between sections
                Layout.fillWidth: true
            }

            MatrixText {
                text: "INFO"
                font.bold: true
            }

            Item {
                Layout.fillWidth: true
            }

            MatrixText {
                text: "Internal ID"
            }

            MatrixText {
                text: roomSettings.roomId
                font.pixelSize: 14
                Layout.alignment: Qt.AlignRight
            }

            MatrixText {
                text: "Room Version"
            }

            MatrixText {
                text: roomSettings.roomVersion
                font.pixelSize: 14
                Layout.alignment: Qt.AlignRight
            }

        }

        Button {
            Layout.alignment: Qt.AlignRight
            text: "OK"
            onClicked: close()
        }

    }

}
