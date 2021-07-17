// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3
import QtQuick.Window 2.3
import im.nheko 1.0
import im.nheko.RoomDirectoryModel 1.0

ApplicationWindow {
    id: roomDirectoryWindow
    visible: true

    x: MainWindow.x + (MainWindow.width / 2) - (width / 2)
    y: MainWindow.y + (MainWindow.height / 2) - (height / 2)
    minimumWidth: 420
    minimumHeight: 650
    palette: Nheko.colors
    color: Nheko.colors.window
    modality: Qt.WindowModal
    flags: Qt.Dialog
    title: qsTr("Explore Public Rooms")

    ListView {
        id: roomDirView
        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height
        model: RoomDirectoryModel {}
        delegate: Rectangle {
            id: roomDirDelegate

            property color background: Nheko.colors.window
            property color importantText: Nheko.colors.text
            property color unimportantText: Nheko.colors.buttonText
            property color bubbleBackground: Nheko.colors.highlight
            property color bubbleText: Nheko.colors.highlightedText
            property int avatarSize: Math.ceil(fontMetrics.lineSpacing * 2.5)

            color: background
            
            width: parent.width
            height: parent.height

            ColumnLayout {
                Avatar {
                    id: avatar

                    // Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: avatarSize
                    Layout.preferredHeight: avatarSize
                    url: model.avatarUrl.replace("mxc://", "image://MxcImage/")
                    displayName: model.name
                }
            }

            RowLayout {

                spacing: Nheko.paddingMedium
                anchors.fill: parent
                anchors.margins: Nheko.paddingMedium

                Avatar {
                    id: avatar

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: avatarSize
                    Layout.preferredHeight: avatarSize
                    url: model.avatarUrl.replace("mxc://", "image://MxcImage/")
                    displayName: model.name
                }

                // ColumnLayout {
                //     id: descriptionLines

                //     Layout.alignment: Qt.AlignLeft
                //     Layout.fillWidth: true
                //     Layout.minimumWidth: 100
                //     width: parent.width - avatar.width
                //     Layout.preferredWidth: parent.width - avatar.width
                //     spacing: Nheko.paddingSmall

                //     RowLayout {
                //         Layout.fillWidth: true
                //         spacing: 0

                //         ElidedLabel {
                //             Layout.alignment: Qt.AlignBottom
                //             color: roomDirDelegate.importantText
                //             elideWidth: descriptionLines.width - Nheko.paddingMedium
                //             fullText: model.name
                //         }

                //         Item {
                //             Layout.fillWidth: true
                //         }
                //     }

                //     RowLayout {
                //         Layout.fillWidth: true
                //         spacing: 0

                //         Label {
                //             Layout.alignment: Qt.AlignBottom
                //             color: roomDirDelegate.unimportantText
                //             contentWidth: parent.width * 0.4
                //             contentHeight: parent.height
                //             elide: Text.ElideRight
                //             text: model.topic
                //         }
                //     }

                // }
            }
        }
    }  

    // function joinRoom(index) {
    //     // body...
    // }
    
    // function previewRoon(index) {
    //     // body...
    // }
}
