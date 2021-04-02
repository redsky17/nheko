import QtQuick 2.12
import QtGraphicalEffects 1.0
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import im.nheko 1.0
import "./emoji"
import "./delegates"

Item {
    id: popupRoot

    // TODO: make this use permissions etc. like the desktop menu
    function show(attachment, messageModel) {
        attached = attachment
        model = messageModel
        visible = true
        popup.state = "shown"
    }

    function hide() {
        popup.state = "hidden"
        visible = false
        attached = undefined
        model = undefined
    }

    property Item attached: null
    property alias model: row.model

    Rectangle {
        id: popup

        radius: 20
        y: timelineRoot.height + 1
        z: 20
        anchors.bottom: parent.bottom
        height: 75
        width: parent.width
        color: colors.window

        // TODO: make this work
        states: [
            State {
                name: "hidden"
                PropertyChanges {
                    target: popup
                    y: timelineRoot.height + 1 // hidden
                    visible: false
                }
            },
            State {
                name: "shown"
                PropertyChanges {
                    target: popup
                    y: timelineRoot.height - popup.height
                    visible: true
                }
            }
        ]
        state: "hidden"

        transitions: Transition {
            from: "hidden"
            to: "shown"
            reversible: true

            NumberAnimation {
                target: popup
                property: y
                duration: 5000
                easing.type: Easing.InOutQuad
                alwaysRunToEnd: true
            }
        }

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

    Rectangle {
        id: overlay
        anchors.fill: parent
        z: popupBottomBar.z - 1

        color: "gray"
        opacity: 0.5

        TapHandler {
            onTapped: popupRoot.hide()
        }
    }

    // TODO: this needs some love
    FastBlur {
        z: overlay.z - 1
        anchors.fill: parent
        source: timelineRoot
        radius: 50
    }
}
