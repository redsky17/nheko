// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import "./components"
import "./delegates"
import "./device-verification"
import "./emoji"
import "./ui"
import "./voip"
import Qt.labs.platform 1.1 as Platform
import QtGraphicalEffects 1.0
import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import im.nheko 1.0
import im.nheko.EmojiModel 1.0

Item {
    id: timelineView

    property var room: null
    property var roomPreview: null
    property bool showBackButton: false

    Label {
        visible: !room && !TimelineManager.isInitialSync && !roomPreview
        anchors.centerIn: parent
        text: qsTr("No room open")
        font.pointSize: 24
        color: Nheko.colors.text
    }

    Spinner {
        visible: TimelineManager.isInitialSync
        anchors.centerIn: parent
        foreground: Nheko.colors.mid
        running: TimelineManager.isInitialSync
        // height is somewhat arbitrary here... don't set width because width scales w/ height
        height: parent.height / 16
        z: 3
    }

    ColumnLayout {
        id: timelineLayout

        visible: room != null && !room.isSpace
        enabled: visible
        anchors.fill: parent
        spacing: 0

        TopBar {
            showBackButton: timelineView.showBackButton
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            z: 3
            color: Nheko.theme.separator
        }

        Rectangle {
            id: msgView

            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Nheko.colors.base

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                StackLayout {
                    id: stackLayout

                    currentIndex: 0

                    Connections {
                        function onRoomChanged() {
                            stackLayout.currentIndex = 0;
                        }

                        target: timelineView
                    }

                    MessageView {
                        Layout.fillWidth: true
                        implicitHeight: msgView.height - typingIndicator.height
                    }

                    Loader {
                        source: CallManager.isOnCall && CallManager.callType != CallType.VOICE ? "voip/VideoCall.qml" : ""
                        onLoaded: TimelineManager.setVideoCallItem()
                    }

                }

                TypingIndicator {
                    id: typingIndicator
                }

            }

        }

        CallInviteBar {
            id: callInviteBar

            Layout.fillWidth: true
            z: 3
        }

        ActiveCallBar {
            Layout.fillWidth: true
            z: 3
        }

        Rectangle {
            Layout.fillWidth: true
            z: 3
            height: 1
            color: Nheko.theme.separator
        }

        ReplyPopup {
        }

        MessageInput {
        }

    }

    ColumnLayout {
        id: preview

        property string roomName: room ? room.roomName : (roomPreview ? roomPreview.roomName : "")
        property string roomTopic: room ? room.roomTopic : (roomPreview ? roomPreview.roomTopic : "")
        property string avatarUrl: room ? room.roomAvatarUrl : (roomPreview ? roomPreview.roomAvatarUrl : "")

        visible: room != null && room.isSpace || roomPreview != null
        enabled: visible
        anchors.fill: parent
        anchors.margins: Nheko.paddingLarge
        spacing: Nheko.paddingLarge

        Item {
            Layout.fillHeight: true
        }

        Avatar {
            url: parent.avatarUrl.replace("mxc://", "image://MxcImage/")
            displayName: parent.roomName
            height: 130
            width: 130
            Layout.alignment: Qt.AlignHCenter
            enabled: false
        }

        MatrixText {
            text: parent.roomName
            font.pixelSize: 24
            Layout.alignment: Qt.AlignHCenter
        }

        MatrixText {
            visible: !!room
            text: qsTr("%1 member(s)").arg(room ? room.roomMemberCount : 0)
            Layout.alignment: Qt.AlignHCenter
        }

        ScrollView {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.leftMargin: Nheko.paddingLarge
            Layout.rightMargin: Nheko.paddingLarge

            TextArea {
                text: TimelineManager.escapeEmoji(preview.roomTopic)
                wrapMode: TextEdit.WordWrap
                textFormat: TextEdit.RichText
                readOnly: true
                background: null
                selectByMouse: true
                color: Nheko.colors.text
                horizontalAlignment: TextEdit.AlignHCenter
                onLinkActivated: Nheko.openLink(link)

                CursorShape {
                    anchors.fill: parent
                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                }

            }

        }

        FlatButton {
            visible: roomPreview && !roomPreview.isInvite
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("join the conversation")
            onClicked: Rooms.joinPreview(roomPreview.roomid)
        }

        FlatButton {
            visible: roomPreview && roomPreview.isInvite
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("accept invite")
            onClicked: Rooms.acceptInvite(roomPreview.roomid)
        }

        FlatButton {
            visible: roomPreview && roomPreview.isInvite
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("decline invite")
            onClicked: Rooms.declineInvite(roomPreview.roomid)
        }

        Item {
            visible: room != null
            Layout.preferredHeight: Math.ceil(fontMetrics.lineSpacing * 2)
        }

        Item {
            Layout.fillHeight: tru
        }

    }

    ImageButton {
        id: backToRoomsButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Nheko.paddingMedium
        width: Nheko.avatarSize
        height: Nheko.avatarSize
        visible: room != null && room.isSpace && showBackButton
        enabled: visible
        image: ":/icons/icons/ui/angle-pointing-to-left.png"
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Back to room list")
        onClicked: Rooms.resetCurrentRoom()
    }

    NhekoDropArea {
        anchors.fill: parent
        roomid: room ? room.roomId() : ""
    }

    Connections {
        target: room
        onOpenRoomSettingsDialog: {
            var roomSettings = roomSettingsComponent.createObject(timelineRoot, {
                "roomSettings": settings
            });
            roomSettings.show();
        }
    }

    MobileMessageContextPopup {
        id: mobileContextPopup

        visible: false
        anchors.fill: parent
    }

}

