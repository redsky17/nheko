// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import "./delegates"
import "./emoji"
import Qt.labs.platform 1.1 as Platform
import QtGraphicalEffects 1.0
import QtQuick 2.12
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.2
import QtQuick.Window 2.2
import im.nheko 1.0

ScrollView {
    clip: false
    palette: Nheko.colors
    padding: 8
    ScrollBar.horizontal.visible: false

    ListView {
        id: chat

        property int delegateMaxWidth: ((Settings.timelineMaxWidth > 100 && Settings.timelineMaxWidth < parent.availableWidth) ? Settings.timelineMaxWidth : parent.availableWidth) - parent.padding * 2

        model: room
        boundsBehavior: Flickable.StopAtBounds
        pixelAligned: true
        spacing: 4
        verticalLayoutDirection: ListView.BottomToTop
        onCountChanged: {
            // Mark timeline as read
            if (atYEnd)
                model.currentIndex = 0;

        }

        Rectangle {
            //closePolicy: Popup.NoAutoClose

            id: messageActions

            property Item attached: null
            property alias model: row.model
            // use comma to update on scroll
            property var attachedPos: chat.contentY, attached ? chat.mapFromItem(attached, attached ? attached.width - width : 0, -height) : null
            readonly property int padding: 4

            visible: Settings.buttonsInTimeline && !!attached && (attached.hovered || messageActionHover.hovered)
            x: attached ? attachedPos.x : 0
            y: attached ? attachedPos.y : 0
            z: 10
            height: row.implicitHeight + padding * 2
            width: row.implicitWidth + padding * 2
            color: Nheko.colors.window
            border.color: Nheko.colors.buttonText
            border.width: 1
            radius: padding

            HoverHandler {
                id: messageActionHover

                grabPermissions: PointerHandler.CanTakeOverFromAnything
            }

            Row {
                id: row

                property var model

                anchors.centerIn: parent
                spacing: messageActions.padding

                ImageButton {
                    id: editButton

                    visible: !!row.model && row.model.isEditable
                    buttonTextColor: Nheko.colors.buttonText
                    width: 16
                    hoverEnabled: true
                    image: ":/icons/icons/ui/edit.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Edit")
                    onClicked: {
                        if (row.model.isEditable)
                            chat.model.editAction(row.model.id);

                    }
                }

                EmojiButton {
                    id: reactButton

                    visible: chat.model ? chat.model.permissions.canSend(MtxEvent.Reaction) : false
                    width: 16
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("React")
                    emojiPicker: emojiPopup
                    event_id: row.model ? row.model.id : ""
                }

                ImageButton {
                    id: replyButton

                    visible: chat.model ? chat.model.permissions.canSend(MtxEvent.TextMessage) : false
                    width: 16
                    hoverEnabled: true
                    image: ":/icons/icons/ui/mail-reply.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Reply")
                    onClicked: chat.model.replyAction(row.model.id)
                }

                ImageButton {
                    id: optionsButton

                    width: 16
                    hoverEnabled: true
                    image: ":/icons/icons/ui/vertical-ellipsis.png"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Options")
                    onClicked: messageContextMenu.show(row.model.id, row.model.type, row.model.isSender, row.model.isEncrypted, row.model.isEditable, "", row.model.body, optionsButton)
                }

            }

        }

        ScrollHelper {
            flickable: parent
            anchors.fill: parent
            enabled: !Settings.mobileMode
        }

        Shortcut {
            sequence: StandardKey.MoveToPreviousPage
            onActivated: {
                chat.contentY = chat.contentY - chat.height / 2;
                chat.returnToBounds();
            }
        }

        Shortcut {
            sequence: StandardKey.MoveToNextPage
            onActivated: {
                chat.contentY = chat.contentY + chat.height / 2;
                chat.returnToBounds();
            }
        }

        Shortcut {
            sequence: StandardKey.Cancel
            onActivated: {
                if (chat.model.reply)
                    chat.model.reply = undefined;
                else
                    chat.model.edit = undefined;
            }
        }

        Shortcut {
            sequence: "Alt+Up"
            onActivated: chat.model.reply = chat.model.indexToId(chat.model.reply ? chat.model.idToIndex(chat.model.reply) + 1 : 0)
        }

        Shortcut {
            sequence: "Alt+Down"
            onActivated: {
                var idx = chat.model.reply ? chat.model.idToIndex(chat.model.reply) - 1 : -1;
                chat.model.reply = idx >= 0 ? chat.model.indexToId(idx) : null;
            }
        }

        Shortcut {
            sequence: "Alt+F"
            onActivated: {
                if (chat.model.reply) {
                    var forwardMess = forwardCompleterComponent.createObject(timelineRoot);
                    forwardMess.setMessageEventId(chat.model.reply);
                    forwardMess.open();
                    chat.model.reply = null;
                }
            }
        }

        Shortcut {
            sequence: "Ctrl+E"
            onActivated: {
                chat.model.edit = chat.model.reply;
            }
        }

        Connections {
            target: TimelineManager
            onFocusChanged: readTimer.running = TimelineManager.isWindowFocused
        }

        Timer {
            id: readTimer

            // force current read index to update
            onTriggered: chat.model.setCurrentIndex(chat.model.currentIndex)
            interval: 1000
        }

        Component {
            id: sectionHeader

            Column {
                topPadding: 4
                bottomPadding: 4
                spacing: 8
                visible: modelData && (modelData.previousMessageUserId !== modelData.userId || modelData.previousMessageDay !== modelData.day)
                width: parentWidth
                height: ((modelData && modelData.previousMessageDay !== modelData.day) ? dateBubble.height + 8 + userName.height : userName.height) + 8

                Label {
                    id: dateBubble

                    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                    visible: modelData && modelData.previousMessageDay !== modelData.day
                    text: modelData ? chat.model.formatDateSeparator(modelData.timestamp) : ""
                    color: Nheko.colors.text
                    height: Math.round(fontMetrics.height * 1.4)
                    width: contentWidth * 1.2
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter

                    background: Rectangle {
                        radius: parent.height / 2
                        color: Nheko.colors.window
                    }

                }

                Row {
                    height: userName.height
                    spacing: 8

                    Avatar {
                        id: messageUserAvatar

                        property int avatarSize: Math.ceil(fontMetrics.lineSpacing * 1.5)

                        width: avatarSize
                        height: avatarSize
                        y: -Math.ceil(fontMetrics.lineSpacing * 0.25)
                        url: modelData ? chat.model.avatarUrl(modelData.userId).replace("mxc://", "image://MxcImage/") : ""
                        displayName: modelData ? modelData.userName : ""
                        userid: modelData ? modelData.userId : ""
                        onClicked: chat.model.openUserProfile(modelData.userId)
                        ToolTip.visible: avatarHover.hovered
                        ToolTip.text: userid

                        HoverHandler {
                            id: avatarHover
                        }

                    }

                    Connections {
                        target: chat.model
                        onRoomAvatarUrlChanged: {
                            messageUserAvatar.url = modelData ? chat.model.avatarUrl(modelData.userId).replace("mxc://", "image://MxcImage/") : "";
                        }
                        onScrollToIndex: chat.positionViewAtIndex(index, ListView.Visible)
                    }

                    Label {
                        id: userName

                        text: modelData ? TimelineManager.escapeEmoji(modelData.userName) : ""
                        color: TimelineManager.userColor(modelData ? modelData.userId : "", Nheko.colors.window)
                        textFormat: Text.RichText
                        ToolTip.visible: displayNameHover.hovered
                        ToolTip.text: modelData ? modelData.userId : ""

                        TapHandler {
                            onSingleTapped: chat.model.openUserProfile(modelData.userId)
                        }

                        CursorShape {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                        }

                        HoverHandler {
                            id: displayNameHover
                        }

                    }

                    Label {
                        color: Nheko.colors.buttonText
                        text: modelData ? TimelineManager.userStatus(modelData.userId) : ""
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        width: chat.delegateMaxWidth - parent.spacing * 2 - userName.implicitWidth - Nheko.avatarSize
                        font.italic: true
                    }

                }

            }

        }

        delegate: Item {
            id: wrapper

            property bool scrolledToThis: model.id === chat.model.scrollTarget && (y + height > chat.y + chat.contentY && y < chat.y + chat.height + chat.contentY)

            anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
            width: chat.delegateMaxWidth
            height: section ? section.height + timelinerow.height : timelinerow.height

            Rectangle {
                id: scrollHighlight

                opacity: 0
                visible: true
                anchors.fill: timelinerow
                color: Nheko.colors.highlight

                states: State {
                    name: "revealed"
                    when: wrapper.scrolledToThis
                }

                transitions: Transition {
                    from: ""
                    to: "revealed"

                    SequentialAnimation {
                        PropertyAnimation {
                            target: scrollHighlight
                            properties: "opacity"
                            easing.type: Easing.InOutQuad
                            from: 0
                            to: 1
                            duration: 500
                        }

                        PropertyAnimation {
                            target: scrollHighlight
                            properties: "opacity"
                            easing.type: Easing.InOutQuad
                            from: 1
                            to: 0
                            duration: 500
                        }

                        ScriptAction {
                            script: chat.model.eventShown()
                        }

                    }

                }

            }

            Loader {
                id: section

                property var modelData: model
                property int parentWidth: parent.width

                active: model.previousMessageUserId !== undefined && model.previousMessageUserId !== model.userId || model.previousMessageDay !== model.day
                //asynchronous: true
                sourceComponent: sectionHeader
                visible: status == Loader.Ready
            }

            TimelineRow {
                id: timelinerow

                property alias hovered: hoverHandler.hovered

                y: section.visible && section.active ? section.y + section.height : 0

                HoverHandler {
                    id: hoverHandler

                    enabled: !Settings.mobileMode
                    onHoveredChanged: {
                        if (hovered) {
                            if (!messageActionHover.hovered) {
                                messageActions.attached = timelinerow;
                                messageActions.model = model;
                            }
                        }
                    }
                }

            }

            Connections {
                target: chat
                onMovementEnded: {
                    if (y + height + 2 * chat.spacing > chat.contentY + chat.height && y < chat.contentY + chat.height)
                        chat.model.currentIndex = index;

                }
            }

        }

        footer: BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: chat.model && chat.model.paginationInProgress
            height: 50
            width: 50
            z: 3
        }

    }

    Platform.Menu {
        id: messageContextMenu

        property string eventId
        property string link
        property string text
        property int eventType
        property bool isEncrypted
        property bool isEditable
        property bool isSender

        function show(eventId_, eventType_, isSender_, isEncrypted_, isEditable_, link_, text_, showAt_) {
            eventId = eventId_;
            eventType = eventType_;
            isEncrypted = isEncrypted_;
            isEditable = isEditable_;
            isSender = isSender_;
            if (text_)
                text = text_;
            else
                text = "";
            if (link_)
                link = link_;
            else
                link = "";
            if (showAt_)
                open(showAt_);
            else
                open();
        }

        Platform.MenuItem {
            visible: messageContextMenu.text
            enabled: visible
            text: qsTr("&Copy")
            onTriggered: Clipboard.text = messageContextMenu.text
        }

        Platform.MenuItem {
            visible: messageContextMenu.link
            enabled: visible
            text: qsTr("Copy &link location")
            onTriggered: Clipboard.text = messageContextMenu.link
        }

        Platform.MenuItem {
            id: reactionOption

            visible: room ? room.permissions.canSend(MtxEvent.Reaction) : false
            text: qsTr("Re&act")
            onTriggered: emojiPopup.show(null, function(emoji) {
                room.input.reaction(messageContextMenu.eventId, emoji);
            })
        }

        Platform.MenuItem {
            visible: room ? room.permissions.canSend(MtxEvent.TextMessage) : false
            text: qsTr("Repl&y")
            onTriggered: room.replyAction(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            visible: messageContextMenu.isEditable && (room ? room.permissions.canSend(MtxEvent.TextMessage) : false)
            enabled: visible
            text: qsTr("&Edit")
            onTriggered: room.editAction(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            text: qsTr("Read receip&ts")
            onTriggered: room.readReceiptsAction(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            visible: messageContextMenu.eventType == MtxEvent.ImageMessage || messageContextMenu.eventType == MtxEvent.VideoMessage || messageContextMenu.eventType == MtxEvent.AudioMessage || messageContextMenu.eventType == MtxEvent.FileMessage || messageContextMenu.eventType == MtxEvent.Sticker || messageContextMenu.eventType == MtxEvent.TextMessage || messageContextMenu.eventType == MtxEvent.LocationMessage || messageContextMenu.eventType == MtxEvent.EmoteMessage || messageContextMenu.eventType == MtxEvent.NoticeMessage
            text: qsTr("&Forward")
            onTriggered: {
                var forwardMess = forwardCompleterComponent.createObject(timelineRoot);
                forwardMess.setMessageEventId(messageContextMenu.eventId);
                forwardMess.open();
            }
        }

        Platform.MenuItem {
            text: qsTr("&Mark as read")
        }

        Platform.MenuItem {
            text: qsTr("View raw message")
            onTriggered: room.viewRawMessage(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            // TODO(Nico): Fix this still being iterated over, when using keyboard to select options
            visible: messageContextMenu.isEncrypted
            enabled: visible
            text: qsTr("View decrypted raw message")
            onTriggered: room.viewDecryptedRawMessage(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            visible: (room ? room.permissions.canRedact() : false) || messageContextMenu.isSender
            text: qsTr("Remo&ve message")
            onTriggered: room.redactEvent(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            visible: messageContextMenu.eventType == MtxEvent.ImageMessage || messageContextMenu.eventType == MtxEvent.VideoMessage || messageContextMenu.eventType == MtxEvent.AudioMessage || messageContextMenu.eventType == MtxEvent.FileMessage || messageContextMenu.eventType == MtxEvent.Sticker
            enabled: visible
            text: qsTr("&Save as")
            onTriggered: room.saveMedia(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            visible: messageContextMenu.eventType == MtxEvent.ImageMessage || messageContextMenu.eventType == MtxEvent.VideoMessage || messageContextMenu.eventType == MtxEvent.AudioMessage || messageContextMenu.eventType == MtxEvent.FileMessage || messageContextMenu.eventType == MtxEvent.Sticker
            enabled: visible
            text: qsTr("&Open in external program")
            onTriggered: room.openMedia(messageContextMenu.eventId)
        }

        Platform.MenuItem {
            visible: messageContextMenu.eventId
            enabled: visible
            text: qsTr("Copy link to eve&nt")
            onTriggered: room.copyLinkToEvent(messageContextMenu.eventId)
        }

    }

    Component {
        id: forwardCompleterComponent

        ForwardCompleter {
        }

    }

}
