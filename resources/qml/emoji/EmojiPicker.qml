// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import "../"
import QtGraphicalEffects 1.0
import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.3
import im.nheko 1.0
import im.nheko.EmojiModel 1.0

Menu {
    id: emojiPopup

    property var callback
    property var colors
    property alias model: gridView.model
    property var textArea
    property string emojiCategory: "people"
    property real highlightHue: Nheko.colors.highlight.hslHue
    property real highlightSat: Nheko.colors.highlight.hslSaturation
    property real highlightLight: Nheko.colors.highlight.hslLightness

    function show(showAt, callback) {
        console.debug("Showing emojiPicker");
        emojiPopup.callback = callback;
        if (Settings.mobileMode)
            popup(0, timelineRoot.height - height, null);
        else
            popup(showAt ? showAt : null);
    }

    margins: 0
    bottomPadding: 1
    leftPadding: 1
    rightPadding: 1
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    width: Settings.mobileMode ? timelineRoot.width : 7 * 52 + 20
    x: Settings.mobileMode ? 0 : undefined
    y: Settings.mobileMode ? timelineRoot.height - height : undefined

    Rectangle {
        color: Nheko.colors.window
        height: columnView.implicitHeight + 4
        width: 7 * 52 + 20

        ColumnLayout {
            id: columnView

            spacing: 0
            anchors.leftMargin: 3
            anchors.rightMargin: 3
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 2

            // Search field
            TextField {
                id: emojiSearch

                Layout.topMargin: 3
                Layout.preferredWidth: 7 * 52 + 20 - 6
                palette: Nheko.colors
                background: null
                placeholderTextColor: Nheko.colors.buttonText
                color: Nheko.colors.text
                placeholderText: qsTr("Search")
                selectByMouse: true
                rightPadding: clearSearch.width
                onTextChanged: searchTimer.restart()
                onVisibleChanged: {
                    if (visible)
                        forceActiveFocus();
                }

                Timer {
                    id: searchTimer

                    interval: 350 // tweak as needed?
                    onTriggered: {
                        emojiPopup.model.searchString = emojiSearch.text;
                        emojiPopup.model.category = Emoji.Category.Search;
                    }
                }

                ToolButton {
                    id: clearSearch

                    visible: emojiSearch.text !== ''
                    icon.source: "image://colorimage/:/icons/icons/ui/round-remove-button.png?" + (clearSearch.hovered ? Nheko.colors.highlight : Nheko.colors.buttonText)
                    focusPolicy: Qt.NoFocus
                    onClicked: emojiSearch.clear()
                    hoverEnabled: true
                    background: null

                    anchors {
                        verticalCenter: parent.verticalCenter
                        right: parent.right
                    }
                    // clear the default hover effects.

                    Image {
                        height: parent.height - 2 * Nheko.paddingSmall
                        width: height
                        source: "image://colorimage/:/icons/icons/ui/round-remove-button.png?" + (clearSearch.hovered ? Nheko.colors.highlight : Nheko.colors.buttonText)

                        anchors {
                            verticalCenter: parent.verticalCenter
                            right: parent.right
                            margins: Nheko.paddingSmall
                        }

                    }

                }

            }

            // emoji grid
            GridView {
                id: gridView

                Layout.preferredHeight: cellHeight * 5
                Layout.preferredWidth: 7 * 52 + 20
                Layout.maximumWidth: timelineRoot.width
                Layout.fillWidth: true
                Layout.leftMargin: 4
                cellWidth: 52
                cellHeight: 52
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                currentIndex: -1 // prevent sorting from stealing focus
                cacheBuffer: 500

                // Individual emoji
                delegate: AbstractButton {
                    width: 48
                    height: 48
                    hoverEnabled: true
                    ToolTip.text: model.shortName
                    ToolTip.visible: hovered
                    // TODO: maybe add favorites at some point?
                    onClicked: {
                        console.debug("Picked " + model.unicode);
                        emojiPopup.close();
                        callback(model.unicode);
                    }

                    // give the emoji a little oomf
                    DropShadow {
                        width: parent.width
                        height: parent.height
                        horizontalOffset: 3
                        verticalOffset: 3
                        radius: 8
                        samples: 17
                        color: "#80000000"
                        source: parent.contentItem
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.family: Settings.emojiFont
                        font.pixelSize: 36
                        text: model.unicode
                        color: Nheko.colors.text
                    }

                    background: Rectangle {
                        anchors.fill: parent
                        color: hovered ? Nheko.colors.highlight : 'transparent'
                        radius: 5
                    }

                }

                ScrollBar.vertical: ScrollBar {
                    id: emojiScroll
                }

            }

            // Separator
            Rectangle {
                visible: emojiSearch.text === ''
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: emojiPopup.Nheko.theme.separator
            }

            // Category picker row
            RowLayout {
                visible: emojiSearch.text === ''
                Layout.bottomMargin: 0
                Layout.preferredHeight: 42
                implicitHeight: 42
                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom

                // Display the normal categories
                Repeater {

                    model: ListModel {
                        // TODO: Would like to get 'simple' icons for the categories
                        ListElement {
                            image: ":/icons/icons/emoji-categories/people.png"
                            category: Emoji.Category.People
                        }

                        ListElement {
                            image: ":/icons/icons/emoji-categories/nature.png"
                            category: Emoji.Category.Nature
                        }

                        ListElement {
                            image: ":/icons/icons/emoji-categories/foods.png"
                            category: Emoji.Category.Food
                        }

                        ListElement {
                            image: ":/icons/icons/emoji-categories/activity.png"
                            category: Emoji.Category.Activity
                        }

                        ListElement {
                            image: ":/icons/icons/emoji-categories/travel.png"
                            category: Emoji.Category.Travel
                        }

                        ListElement {
                            image: ":/icons/icons/emoji-categories/objects.png"
                            category: Emoji.Category.Objects
                        }

                        ListElement {
                            image: ":/icons/icons/emoji-categories/symbols.png"
                            category: Emoji.Category.Symbols
                        }

                        ListElement {
                            image: ":/icons/icons/emoji-categories/flags.png"
                            category: Emoji.Category.Flags
                        }

                    }

                    delegate: AbstractButton {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        hoverEnabled: true
                        ToolTip.text: {
                            switch (model.category) {
                            case Emoji.Category.People:
                                return qsTr('People');
                            case Emoji.Category.Nature:
                                return qsTr('Nature');
                            case Emoji.Category.Food:
                                return qsTr('Food');
                            case Emoji.Category.Activity:
                                return qsTr('Activity');
                            case Emoji.Category.Travel:
                                return qsTr('Travel');
                            case Emoji.Category.Objects:
                                return qsTr('Objects');
                            case Emoji.Category.Symbols:
                                return qsTr('Symbols');
                            case Emoji.Category.Flags:
                                return qsTr('Flags');
                            }
                        }
                        ToolTip.visible: hovered
                        onClicked: {
                            //emojiPopup.model.category = model.category;
                            gridView.positionViewAtIndex(emojiPopup.model.sourceModel.categoryToIndex(model.category), GridView.Beginning);
                        }

                        MouseArea {
                            id: mouseArea

                            anchors.fill: parent
                            onPressed: mouse.accepted = false
                            cursorShape: Qt.PointingHandCursor
                        }

                        contentItem: Image {
                            horizontalAlignment: Image.AlignHCenter
                            verticalAlignment: Image.AlignVCenter
                            fillMode: Image.Pad
                            sourceSize.width: 32
                            sourceSize.height: 32
                            source: "image://colorimage/" + model.image + "?" + (hovered ? Nheko.colors.highlight : Nheko.colors.buttonText)
                        }

                        background: Rectangle {
                            anchors.fill: parent
                            color: emojiPopup.model.category === model.category ? Qt.hsla(highlightHue, highlightSat, highlightLight, 0.2) : 'transparent'
                            radius: 5
                            border.color: emojiPopup.model.category === model.category ? Nheko.colors.highlight : 'transparent'
                        }

                    }

                }

            }

        }

    }

}
