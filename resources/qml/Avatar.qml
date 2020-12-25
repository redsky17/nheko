import "./ui"
import QtGraphicalEffects 1.0
import QtQuick 2.6
import QtQuick.Controls 2.3
import im.nheko 1.0

Rectangle {
    id: avatar

    property alias url: img.source
    property string userid
    property string displayName

    signal clicked(var mouse)

    width: 48
    height: 48
    radius: Settings.avatarCircles ? height / 2 : 3
    color: colors.alternateBase
    Component.onCompleted: {
        mouseArea.clicked.connect(clicked);
    }

    Label {
        anchors.fill: parent
        text: TimelineManager.escapeEmoji(displayName ? String.fromCodePoint(displayName.codePointAt(0)) : "")
        textFormat: Text.RichText
        font.pixelSize: avatar.height / 2
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        visible: img.status != Image.Ready && !Settings.useIdenticon
        color: colors.text
    }

    Image {
        id: identicon
        anchors.fill: parent
        visible: img.status != Image.Ready && Settings.useIdenticon
        source: "image://jdenticon/" + userid
    }

    Image {
        id: img

        anchors.fill: parent
        asynchronous: true
        fillMode: Image.PreserveAspectCrop
        mipmap: true
        smooth: true
        sourceSize.width: avatar.width
        sourceSize.height: avatar.height
        layer.enabled: true

        MouseArea {
            id: mouseArea

            anchors.fill: parent

            Ripple {
                rippleTarget: mouseArea
                color: Qt.rgba(colors.alternateBase.r, colors.alternateBase.g, colors.alternateBase.b, 0.5)
            }

        }

        layer.effect: OpacityMask {

            maskSource: Rectangle {
                anchors.fill: parent
                width: avatar.width
                height: avatar.height
                radius: Settings.avatarCircles ? height / 2 : 3
            }

        }

    }

    Rectangle {
        anchors.bottom: avatar.bottom
        anchors.right: avatar.right
        visible: !!userid
        height: avatar.height / 6
        width: height
        radius: Settings.avatarCircles ? height / 2 : height / 4
        color: {
            switch (TimelineManager.userPresence(userid)) {
            case "online":
                return "#00cc66";
            case "unavailable":
                return "#ff9933";
            case "offline":
            default:
                // return "#a82353" don't show anything if offline, since it is confusing, if presence is disabled
                "transparent";
            }
        }
    }

    CursorShape {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
    }

}
