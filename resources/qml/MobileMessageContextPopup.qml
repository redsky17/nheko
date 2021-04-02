import QtQuick 2.12
import QtGraphicalEffects 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import im.nheko 1.0
import "./emoji"

Item {
    id: popupRoot

    // TODO: make this use permissions etc. like the desktop menu
    function show(attachment, messageModel) {
        attached = attachment
        model = messageModel
        popupRoot.state = "shown"
    }

    function hide() {
        popupRoot.state = "hidden"
        attached = undefined
        model = undefined
    }

    state: "hidden"
    visible: false
    property Item attached: null
    property alias model: row.model

    states: [
        State {
            name: "hidden"

            PropertyChanges {
                target: popupRoot
                visible: false
            }

            PropertyChanges {
                target: popup
                anchors.bottomMargin: -popup.height
                visible: false
            }

            PropertyChanges {
                target: overlay
                visible: false
                opacity: 0
            }
        },
        State {
            name: "shown"

            PropertyChanges {
                target: popupRoot
                visible: true
            }

            PropertyChanges {
                target: popup
                anchors.bottomMargin: 0
                visible: true
            }

            PropertyChanges {
                target: overlay
                visible: true
                opacity: 1
            }
        }
    ]

    transitions: [
            Transition {
            from: "hidden"
            to: "shown"

            SequentialAnimation {
                NumberAnimation {
                    targets: [popupRoot, popup, overlay]
                    properties: "visible"
                    duration: 0
                }

                ParallelAnimation {
                    NumberAnimation {
                        target: popup
                        property: "anchors.bottomMargin"
                        duration: 250
                        easing.type: Easing.InOutQuad
                    }

                    NumberAnimation {
                        target: overlay
                        property: "opacity"
                        duration: 250
                        easing.type: Easing.InQuad
                    }
                }
            }
        },

        Transition {
            from: "shown"
            to: "hidden"

            SequentialAnimation {
                ParallelAnimation {
                    NumberAnimation {
                        target: popup
                        property: "anchors.bottomMargin"
                        duration: 250
                        easing.type: Easing.InOutQuad
                    }

                    NumberAnimation {
                        target: overlay
                        property: "opacity"
                        duration: 250
                        easing.type: Easing.InQuad
                    }
                }

                NumberAnimation {
                    targets: [popupRoot, popup, overlay]
                    properties: "visible"
                    duration: 0
                }
            }
        }
    ]

    Rectangle {
        id: popup

        radius: 20
        z: 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -height
        height: 75
        width: parent.width
        color: colors.window

        RowLayout {
            id: row

            property var model

            spacing: 5
            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 10
            anchors.fill: parent

            // without this, the buttons tend to hug the sides
            anchors.leftMargin: 10
            anchors.rightMargin: anchors.leftMargin

            ImageButton {
                id: editButton

                visible: !!row.model && row.model.isEditable
                buttonTextColor: colors.buttonText
                Layout.minimumWidth: 20
                Layout.preferredWidth: 35
                Layout.preferredHeight: Layout.preferredWidth
                Layout.minimumHeight: Layout.minimumWidth
                height: width
                Layout.alignment: Qt.AlignHCenter
                hoverEnabled: true
                image: ":/icons/icons/ui/edit.png"
                ToolTip.visible: hovered
                ToolTip.text: row.model && row.model.isEditable ? qsTr("Edit") : qsTr("Edited")
                onClicked: {
                    if (row.model.isEditable)
                        TimelineManager.timeline.editAction(row.model.id);
                    popupRoot.hide()
                }
            }

            ImageButton {
                id: deleteButton

                buttonTextColor: colors.buttonText
                Layout.minimumWidth: 20
                Layout.preferredWidth: 35
                Layout.minimumHeight: Layout.minimumWidth
                Layout.preferredHeight: Layout.preferredWidth
                height: width
                Layout.alignment: Qt.AlignHCenter
                hoverEnabled: true
                image: ":/icons/icons/ui/delete.png"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Remove")
                onClicked: {
                    TimelineManager.timeline.redactEvent(row.model.id)
                    popupRoot.hide()
                }
            }

            EmojiButton {
                id: reactButton

                Layout.minimumWidth: 20
                Layout.preferredWidth: 35
                Layout.preferredHeight: Layout.preferredWidth
                Layout.minimumHeight: Layout.minimumWidth
                height: width
                Layout.alignment: Qt.AlignHCenter
                hoverEnabled: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("React")
                emojiPicker: emojiPopup
                event_id: row.model ? row.model.id : ""
            }

            ImageButton {
                id: replyButton

                Layout.minimumWidth: 20
                Layout.preferredWidth: 35
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: Layout.preferredWidth
                Layout.minimumHeight: Layout.minimumWidth
                height: width
                hoverEnabled: true
                image: ":/icons/icons/ui/mail-reply.png"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Reply")
                onClicked: {
                    TimelineManager.timeline.replyAction(row.model.id)
                    popupRoot.hide()
                }
            }

            ImageButton {
                id: optionsButton

                Layout.minimumWidth: 20
                Layout.preferredWidth: 35
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: Layout.preferredWidth
                Layout.minimumHeight: Layout.minimumWidth
                height: width
                hoverEnabled: true
                image: ":/icons/icons/ui/vertical-ellipsis.png"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Options")
                onClicked: messageContextMenu.show(row.model.id, row.model.type, row.model.isEncrypted, row.model.isEditable, optionsButton)
            }
        }

        Rectangle {
            id: popupBottomBar

            z: popup.z - 1
            anchors.bottom: popup.bottom
            height: popup.radius
            width: popup.width
            color: popup.color
        }
    }

    FastBlur {
        id: overlay
        anchors.fill: parent
        source: timelineLayout
        radius: 50
        z: popupBottomBar.z - 1
        visible: false
        opacity: 0

        Rectangle {
            anchors.fill: parent
            z: parent.z - 1
            opacity: 0.5
            color: "gray"
        }

        TapHandler {
            onTapped: popupRoot.hide()
        }
    }
}
