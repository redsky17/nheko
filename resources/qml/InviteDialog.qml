// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import im.nheko 1.0

ApplicationWindow {
    id: inviteDialogRoot

    property string roomId
    property string plainRoomName
    property InviteesModel invitees

    function addInvite() {
        if (inviteeEntry.isValidMxid) {
            invitees.addUser(inviteeEntry.text);
            inviteeEntry.clear();
        }
    }

    function cleanUpAndClose() {
        if (inviteeEntry.isValidMxid)
            addInvite();

        invitees.accept();
        close();
    }

    title: qsTr("Invite users to %1").arg(plainRoomName)
    x: MainWindow.x + (MainWindow.width / 2) - (width / 2)
    y: MainWindow.y + (MainWindow.height / 2) - (height / 2)
    height: 380
    width: 340
    palette: Nheko.colors
    color: Nheko.colors.window

    Shortcut {
        sequence: "Ctrl+Enter"
        onActivated: cleanUpAndClose()
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: inviteDialogRoot.close()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Nheko.paddingMedium
        spacing: Nheko.paddingMedium

        Label {
            text: qsTr("User ID to invite")
            Layout.fillWidth: true
            color: Nheko.colors.text
        }

        RowLayout {
            spacing: Nheko.paddingMedium

            MatrixTextField {
                id: inviteeEntry

                property bool isValidMxid: text.match("@.+?:.{3,}")

                backgroundColor: Nheko.colors.window
                placeholderText: qsTr("@joe:matrix.org", "Example user id. The name 'joe' can be localized however you want.")
                Layout.fillWidth: true
                onAccepted: {
                    if (isValidMxid)
                        addInvite();

                }
                Component.onCompleted: forceActiveFocus()
                Keys.onShortcutOverride: event.accepted = ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier))
                Keys.onPressed: {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers === Qt.ControlModifier))
                        cleanUpAndClose();

                }
            }

            Button {
                text: qsTr("Add")
                enabled: inviteeEntry.isValidMxid
                onClicked: addInvite()
            }

        }

        ListView {
            id: inviteesList

            Layout.fillWidth: true
            Layout.fillHeight: true
            model: invitees

            delegate: RowLayout {
                spacing: Nheko.paddingMedium

                Avatar {
                    width: Nheko.avatarSize
                    height: Nheko.avatarSize
                    userid: model.mxid
                    url: model.avatarUrl.replace("mxc://", "image://MxcImage/")
                    displayName: model.displayName
                    onClicked: TimelineManager.openGlobalUserProfile(model.mxid)
                }

                ColumnLayout {
                    spacing: Nheko.paddingSmall

                    Label {
                        text: model.displayName
                        color: TimelineManager.userColor(model ? model.mxid : "", Nheko.colors.window)
                        font.pointSize: fontMetrics.font.pointSize
                    }

                    Label {
                        text: model.mxid
                        color: Nheko.colors.buttonText
                        font.pointSize: fontMetrics.font.pointSize * 0.9
                    }

                    Item {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                    }

                }

            }

        }

    }

    footer: DialogButtonBox {
        id: buttons

        Button {
            text: qsTr("Invite")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            enabled: invitees.count > 0
            onClicked: cleanUpAndClose()
        }

        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
            onClicked: inviteDialogRoot.close()
        }

    }

}
