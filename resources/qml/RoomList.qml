// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import "./dialogs"
import Qt.labs.platform 1.1 as Platform
import QtQml 2.12
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.3
import im.nheko 1.0

Page {
    //leftPadding: Nheko.paddingSmall
    //rightPadding: Nheko.paddingSmall
    property int avatarSize: Math.ceil(fontMetrics.lineSpacing * 2.3)
    property bool collapsed: false

    Component {
        id: roomDirectoryComponent

        RoomDirectory {
        }
    }

    ListView {
        id: roomlist

        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height
        model: Rooms
        reuseItems: true

        ScrollHelper {
            flickable: parent
            anchors.fill: parent
            enabled: !Settings.mobileMode
        }

        Connections {
            onActiveTimelineChanged: {
                roomlist.positionViewAtIndex(Rooms.roomidToIndex(Rooms.currentRoom.roomId()), ListView.Contain);
                console.log("Test" + Rooms.currentRoom.roomId() + " " + Rooms.roomidToIndex(Rooms.currentRoom.roomId()));
            }
            target: TimelineManager
        }

        Platform.Menu {
            id: roomContextMenu

            property string roomid
            property var tags

            function show(roomid_, tags_) {
                roomid = roomid_;
                tags = tags_;
                open();
            }

            InputDialog {
                id: newTag

                title: qsTr("New tag")
                prompt: qsTr("Enter the tag you want to use:")
                onAccepted: function(text) {
                    Rooms.toggleTag(roomContextMenu.roomid, "u." + text, true);
                }
            }

            Platform.MenuItem {
                text: qsTr("Leave room")
                onTriggered: Rooms.leave(roomContextMenu.roomid)
            }

            Platform.MenuSeparator {
                text: qsTr("Tag room as:")
            }

            Instantiator {
                model: Communities.tagsWithDefault
                onObjectAdded: roomContextMenu.insertItem(index + 2, object)
                onObjectRemoved: roomContextMenu.removeItem(object)

                delegate: Platform.MenuItem {
                    property string t: modelData

                    text: {
                        switch (t) {
                        case "m.favourite":
                            return qsTr("Favourite");
                        case "m.lowpriority":
                            return qsTr("Low priority");
                        case "m.server_notice":
                            return qsTr("Server notice");
                        default:
                            return t.substring(2);
                        }
                    }
                    checkable: true
                    checked: roomContextMenu.tags !== undefined && roomContextMenu.tags.includes(t)
                    onTriggered: Rooms.toggleTag(roomContextMenu.roomid, t, checked)
                }

            }

            Platform.MenuItem {
                text: qsTr("Create new tag...")
                onTriggered: newTag.show()
            }

        }

        delegate: Rectangle {
            id: roomItem

            property color background: Nheko.colors.window
            property color importantText: Nheko.colors.text
            property color unimportantText: Nheko.colors.buttonText
            property color bubbleBackground: Nheko.colors.highlight
            property color bubbleText: Nheko.colors.highlightedText
            required property string roomName
            required property string roomId
            required property string avatarUrl
            required property string time
            required property string lastMessage
            required property var tags
            required property bool isInvite
            required property bool isSpace
            required property int notificationCount
            required property bool hasLoudNotification
            required property bool hasUnreadMessages

            color: background
            height: avatarSize + 2 * Nheko.paddingMedium
            width: ListView.view.width
            state: "normal"
            ToolTip.visible: hovered.hovered && collapsed
            ToolTip.text: roomName
            states: [
                State {
                    name: "highlight"
                    when: hovered.hovered && !((Rooms.currentRoom && roomId == Rooms.currentRoom.roomId()) || Rooms.currentRoomPreview.roomid == roomId)

                    PropertyChanges {
                        target: roomItem
                        background: Nheko.colors.dark
                        importantText: Nheko.colors.brightText
                        unimportantText: Nheko.colors.brightText
                        bubbleBackground: Nheko.colors.highlight
                        bubbleText: Nheko.colors.highlightedText
                    }

                },
                State {
                    name: "selected"
                    when: (Rooms.currentRoom && roomId == Rooms.currentRoom.roomId()) || Rooms.currentRoomPreview.roomid == roomId

                    PropertyChanges {
                        target: roomItem
                        background: Nheko.colors.highlight
                        importantText: Nheko.colors.highlightedText
                        unimportantText: Nheko.colors.highlightedText
                        bubbleBackground: Nheko.colors.highlightedText
                        bubbleText: Nheko.colors.highlight
                    }

                }
            ]

            TapHandler {
                margin: -Nheko.paddingSmall
                acceptedButtons: Qt.RightButton
                onSingleTapped: {
                    if (!TimelineManager.isInvite)
                        roomContextMenu.show(roomId, tags);

                }
                gesturePolicy: TapHandler.ReleaseWithinBounds
            }

            TapHandler {
                margin: -Nheko.paddingSmall
                onSingleTapped: Rooms.setCurrentRoom(roomId)
                onLongPressed: {
                    if (!isInvite)
                        roomContextMenu.show(roomId, tags);

                }
            }

            HoverHandler {
                id: hovered

                margin: -Nheko.paddingSmall
            }

            RowLayout {
                spacing: Nheko.paddingMedium
                anchors.fill: parent
                anchors.margins: Nheko.paddingMedium

                Avatar {
                    // In the future we could show an online indicator by setting the userid for the avatar
                    //userid: Nheko.currentUser.userid

                    id: avatar

                    enabled: false
                    Layout.alignment: Qt.AlignVCenter
                    height: avatarSize
                    width: avatarSize
                    url: avatarUrl.replace("mxc://", "image://MxcImage/")
                    displayName: roomName

                    Rectangle {
                        id: collapsedNotificationBubble

                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: -Nheko.paddingSmall
                        visible: collapsed && notificationCount > 0
                        enabled: false
                        Layout.alignment: Qt.AlignRight
                        height: fontMetrics.averageCharacterWidth * 3
                        width: height
                        radius: height / 2
                        color: hasLoudNotification ? Nheko.theme.red : roomItem.bubbleBackground

                        Label {
                            anchors.centerIn: parent
                            width: parent.width * 0.8
                            height: parent.height * 0.8
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            fontSizeMode: Text.Fit
                            font.bold: true
                            font.pixelSize: fontMetrics.font.pixelSize * 0.8
                            color: hasLoudNotification ? "white" : roomItem.bubbleText
                            text: notificationCount > 99 ? "99+" : notificationCount
                        }

                    }

                }

                ColumnLayout {
                    id: textContent

                    visible: !collapsed
                    Layout.alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    Layout.minimumWidth: 100
                    width: parent.width - avatar.width
                    Layout.preferredWidth: parent.width - avatar.width
                    spacing: Nheko.paddingSmall

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        ElidedLabel {
                            Layout.alignment: Qt.AlignBottom
                            color: roomItem.importantText
                            elideWidth: textContent.width - timestamp.width - Nheko.paddingMedium
                            fullText: roomName
                            textFormat: Text.RichText
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Label {
                            id: timestamp

                            visible: !isInvite && !isSpace
                            width: visible ? 0 : undefined
                            Layout.alignment: Qt.AlignRight | Qt.AlignBottom
                            font.pixelSize: fontMetrics.font.pixelSize * 0.9
                            color: roomItem.unimportantText
                            text: time
                        }

                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        visible: !isSpace
                        height: visible ? 0 : undefined

                        ElidedLabel {
                            color: roomItem.unimportantText
                            font.pixelSize: fontMetrics.font.pixelSize * 0.9
                            elideWidth: textContent.width - (notificationBubble.visible ? notificationBubble.width : 0) - Nheko.paddingSmall
                            fullText: lastMessage
                            textFormat: Text.RichText
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            id: notificationBubble

                            visible: notificationCount > 0
                            Layout.alignment: Qt.AlignRight
                            height: fontMetrics.averageCharacterWidth * 3
                            width: height
                            radius: height / 2
                            color: hasLoudNotification ? Nheko.theme.red : roomItem.bubbleBackground

                            Label {
                                anchors.centerIn: parent
                                width: parent.width * 0.8
                                height: parent.height * 0.8
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                fontSizeMode: Text.Fit
                                font.bold: true
                                font.pixelSize: fontMetrics.font.pixelSize * 0.8
                                color: hasLoudNotification ? "white" : roomItem.bubbleText
                                text: notificationCount > 99 ? "99+" : notificationCount
                            }

                        }

                    }

                }

            }

            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                height: parent.height - Nheko.paddingSmall * 2
                width: 3
                color: Nheko.colors.highlight
                visible: hasUnreadMessages
            }

        }

    }

    background: Rectangle {
        color: Nheko.theme.sidebarBackground
    }

    header: ColumnLayout {
        spacing: 0

        Rectangle {
            id: userInfoPanel

            function openUserProfile() {
                Nheko.updateUserProfile();
                var userProfile = userProfileComponent.createObject(timelineRoot, {
                    "profile": Nheko.currentUser
                });
                userProfile.show();
            }

            color: Nheko.colors.window
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignBottom
            Layout.preferredHeight: userInfoGrid.implicitHeight + 2 * Nheko.paddingMedium
            Layout.minimumHeight: 40

            InputDialog {
                id: statusDialog

                title: qsTr("Status Message")
                prompt: qsTr("Enter your status message:")
                onAccepted: function(text) {
                    Nheko.setStatusMessage(text);
                }
            }

            Platform.Menu {
                id: userInfoMenu

                Platform.MenuItem {
                    text: qsTr("Profile settings")
                    onTriggered: userInfoPanel.openUserProfile()
                }

                Platform.MenuItem {
                    text: qsTr("Set status message")
                    onTriggered: statusDialog.show()
                }

            }

            TapHandler {
                margin: -Nheko.paddingSmall
                acceptedButtons: Qt.LeftButton
                onSingleTapped: userInfoPanel.openUserProfile()
                onLongPressed: userInfoMenu.open()
                gesturePolicy: TapHandler.ReleaseWithinBounds
            }

            TapHandler {
                margin: -Nheko.paddingSmall
                acceptedButtons: Qt.RightButton
                onSingleTapped: userInfoMenu.open()
                gesturePolicy: TapHandler.ReleaseWithinBounds
            }

            RowLayout {
                id: userInfoGrid

                property var profile: Nheko.currentUser

                spacing: Nheko.paddingMedium
                anchors.fill: parent
                anchors.margins: Nheko.paddingMedium

                Avatar {
                    id: avatar

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: fontMetrics.lineSpacing * 2
                    Layout.preferredHeight: fontMetrics.lineSpacing * 2
                    url: (userInfoGrid.profile ? userInfoGrid.profile.avatarUrl : "").replace("mxc://", "image://MxcImage/")
                    displayName: userInfoGrid.profile ? userInfoGrid.profile.displayName : ""
                    userid: userInfoGrid.profile ? userInfoGrid.profile.userid : ""
                }

                ColumnLayout {
                    id: col

                    visible: !collapsed
                    Layout.alignment: Qt.AlignLeft
                    Layout.fillWidth: true
                    width: parent.width - avatar.width - logoutButton.width - Nheko.paddingMedium * 2
                    Layout.preferredWidth: parent.width - avatar.width - logoutButton.width - Nheko.paddingMedium * 2
                    spacing: 0

                    ElidedLabel {
                        Layout.alignment: Qt.AlignBottom
                        font.pointSize: fontMetrics.font.pointSize * 1.1
                        font.weight: Font.DemiBold
                        fullText: userInfoGrid.profile ? userInfoGrid.profile.displayName : ""
                        elideWidth: col.width
                    }

                    ElidedLabel {
                        Layout.alignment: Qt.AlignTop
                        color: Nheko.colors.buttonText
                        font.pointSize: fontMetrics.font.pointSize * 0.9
                        elideWidth: col.width
                        fullText: userInfoGrid.profile ? userInfoGrid.profile.userid : ""
                    }

                }

                Item {
                }

                ImageButton {
                    id: logoutButton

                    visible: !collapsed
                    Layout.alignment: Qt.AlignVCenter
                    image: ":/icons/icons/ui/power-button-off.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Logout")
                    onClicked: Nheko.openLogoutDialog()
                }

            }

        }

        Rectangle {
            color: Nheko.theme.separator
            height: 2
            Layout.fillWidth: true
        }

    }

    footer: ColumnLayout {
        spacing: 0

        Rectangle {
            color: Nheko.theme.separator
            height: 1
            Layout.fillWidth: true
        }

        Rectangle {
            color: Nheko.colors.window
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignBottom
            Layout.preferredHeight: buttonRow.implicitHeight
            Layout.minimumHeight: 40

            RowLayout {
                id: buttonRow

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Nheko.paddingMedium

                ImageButton {
                    Layout.fillWidth: true
                    hoverEnabled: true
                    width: 22
                    height: 22
                    image: ":/icons/icons/ui/plus-black-symbol.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Start a new chat")
                    Layout.margins: Nheko.paddingMedium
                    onClicked: roomJoinCreateMenu.open(parent)

                    Platform.Menu {
                        id: roomJoinCreateMenu

                        Platform.MenuItem {
                            text: qsTr("Join a room")
                            onTriggered: Nheko.openJoinRoomDialog()
                        }

                        Platform.MenuItem {
                            text: qsTr("Create a new room")
                            onTriggered: Nheko.openCreateRoomDialog()
                        }

                    }

                }

                ImageButton {
                    visible: !collapsed
                    Layout.fillWidth: true
                    hoverEnabled: true
                    width: 22
                    height: 22
                    image: ":/icons/icons/ui/speech-bubbles-comment-option.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Room directory")
                    Layout.margins: Nheko.paddingMedium
                    onClicked: {
                        console.debug("Roomdir clicked");
                        var win = roomDirectoryComponent.createObject(timelineRoot);
                        win.show();
                    } 
                }

                ImageButton {
                    visible: !collapsed
                    Layout.fillWidth: true
                    hoverEnabled: true
                    width: 22
                    height: 22
                    image: ":/icons/icons/ui/settings.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("User settings")
                    Layout.margins: Nheko.paddingMedium
                    onClicked: Nheko.showUserSettingsPage()
                }

            }

        }

    }

}
