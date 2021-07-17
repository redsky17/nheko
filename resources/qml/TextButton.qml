// SPDX-FileCopyrightText: 2021 Nheko Contributors
//
// SPDX-License-Identifier: GPL-3.0-or-later

import "./ui"
import QtQuick 2.3
import QtQuick.Controls 2.3
import im.nheko 1.0 // for cursor shape

AbstractButton {
    id: button

    property alias cursor: mouseArea.cursorShape
    property color highlightColor: colors.highlight
    property color buttonTextColor: colors.buttonText

    focusPolicy: Qt.NoFocus
    width: 16
    height: 16

    Label {
        id: buttonText

        anchors.fill: parent
        text: button.text
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    CursorShape {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
    }

    Ripple {
        color: Qt.rgba(buttonTextColor.r, buttonTextColor.g, buttonTextColor.b, 0.5)
        clip: false
        rippleTarget: button
    }

}
