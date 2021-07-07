// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import "./delegates"
import "./emoji"
import QtQuick 2.12
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.2
import QtQuick.Window 2.2
import im.nheko 1.0

Item {
    anchors.left: parent.left
    anchors.right: parent.right
    height: row.height

    Rectangle {
        color: (Settings.messageHoverHighlight && hoverHandler.hovered) ? Nheko.colors.alternateBase : "transparent"
        anchors.fill: row
    }

    HoverHandler {
        id: hoverHandler

        acceptedDevices: PointerDevice.GenericPointer
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onSingleTapped: messageContextMenu.show(model.id, model.type, model.isSender, model.isEncrypted, model.isEditable, contentItem.child.hoveredLink, contentItem.child.copyText)
        gesturePolicy: TapHandler.ReleaseWithinBounds
    }

    TapHandler {
        onLongPressed: messageContextMenu.show(model.id, model.type, model.isSender, model.isEncrypted, model.isEditable, contentItem.child.hoveredLink, contentItem.child.copyText)
        onDoubleTapped: chat.model.reply = model.id
        gesturePolicy: TapHandler.ReleaseWithinBounds
    }

    RowLayout {
        id: row

        property int avatarSize: Math.ceil(fontMetrics.lineSpacing * 1.5)

        anchors.rightMargin: 1
        anchors.leftMargin: avatarSize + Nheko.paddingMedium
        anchors.left: parent.left
        anchors.right: parent.right

        Column {
            id: messageCol

            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 4
            Layout.topMargin: 1
            Layout.bottomMargin: 1

            // fancy reply, if this is a reply
            Reply {
                visible: model.replyTo
                modelData: chat.model.getDump(model.replyTo, model.id)
                userColor: TimelineManager.userColor(modelData.userId, Nheko.colors.base)
            }

            // actual message content
            MessageDelegate {
                id: contentItem

                width: parent.width
                modelData: model
            }

            Reactions {
                id: reactionRow

                reactions: model.reactions
                eventId: model.id
            }

        }

        StatusIndicator {
            Layout.alignment: Qt.AlignRight | Qt.AlignTop
            Layout.preferredHeight: 16
            width: 16
        }

        EncryptionIndicator {
            visible: model.isRoomEncrypted
            encrypted: model.isEncrypted
            trust: model.trustlevel
            Layout.alignment: Qt.AlignRight | Qt.AlignTop
            Layout.preferredHeight: 16
            Layout.preferredWidth: 16
        }

        Image {
            visible: model.isEdited || model.id == chat.model.edit
            Layout.alignment: Qt.AlignRight | Qt.AlignTop
            Layout.preferredHeight: 16
            Layout.preferredWidth: 16
            height: 16
            width: 16
            sourceSize.width: 16
            sourceSize.height: 16
            source: "image://colorimage/:/icons/icons/ui/edit.png?" + ((model.id == chat.model.edit) ? Nheko.colors.highlight : Nheko.colors.buttonText)
            ToolTip.visible: editHovered.hovered
            ToolTip.text: qsTr("Edited")

            HoverHandler {
                id: editHovered
            }

        }

    }

    Label {
        id: ts

        property int marginPadding: Math.floor((fontMetrics.font.pixelSize - smallFontMetrics.font.pixelSize) / 2)

        anchors.left: parent.left
        anchors.leftMargin: marginPadding
        anchors.bottom: parent.bottom
        anchors.bottomMargin: (reactionRow.height > 0 ? reactionRow.height + messageCol.spacing : 0) + marginPadding
        text: model.timestamp.toLocaleTimeString(Locale.ShortFormat)
        width: Math.max(implicitWidth, text.length * smallFontMetrics.maximumCharacterWidth)
        font.pixelSize: smallFontMetrics.font.pixelSize
        color: Nheko.inactiveColors.text
        ToolTip.visible: ma.hovered
        ToolTip.text: Qt.formatDateTime(model.timestamp, Qt.DefaultLocaleLongDate)

        FontMetrics {
            id: smallFontMetrics

            font.pixelSize: Math.ceil(fontMetrics.font.pixelSize * 0.6)
        }

        HoverHandler {
            id: ma
        }

    }

}
