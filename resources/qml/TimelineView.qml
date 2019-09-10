import QtQuick 2.6
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.5
import QtGraphicalEffects 1.0

import com.github.nheko 1.0

Rectangle {
	anchors.fill: parent

	SystemPalette { id: colors; colorGroup: SystemPalette.Active }
	SystemPalette { id: inactiveColors; colorGroup: SystemPalette.Disabled }
	color: colors.window

	Text {
		visible: !timelineManager.timeline
		anchors.centerIn: parent
		text: qsTr("No room open")
		font.pointSize: 24
		color: colors.windowText
	}

	ListView {
		id: chat

		cacheBuffer: parent.height

		visible: timelineManager.timeline != null
		anchors.fill: parent

		ScrollBar.vertical: ScrollBar {
			id: scrollbar
			anchors.top: parent.top
			anchors.right: parent.right
			anchors.bottom: parent.bottom
		}

		model: timelineManager.timeline
		spacing: 4
		delegate: RowLayout {
			anchors.leftMargin: 52
			anchors.left: parent.left
			anchors.right: parent.right
			anchors.rightMargin: scrollbar.width

			Loader {
				id: loader
				Layout.fillWidth: true
				Layout.alignment: Qt.AlignTop
				height: item.height

				source: switch(model.type) {
					case MtxEvent.Aliases: return "delegates/Aliases.qml"
					case MtxEvent.Avatar: return "delegates/Avatar.qml"
					case MtxEvent.CanonicalAlias: return "delegates/CanonicalAlias.qml"
					case MtxEvent.Create: return "delegates/Create.qml"
					case MtxEvent.GuestAccess: return "delegates/GuestAccess.qml"
					case MtxEvent.HistoryVisibility: return "delegates/HistoryVisibility.qml"
					case MtxEvent.JoinRules: return "delegates/JoinRules.qml"
					case MtxEvent.Member: return "delegates/Member.qml"
					case MtxEvent.Name: return "delegates/Name.qml"
					case MtxEvent.PowerLevels: return "delegates/PowerLevels.qml"
					case MtxEvent.Topic: return "delegates/Topic.qml"
					case MtxEvent.NoticeMessage: return "delegates/NoticeMessage.qml"
					case MtxEvent.TextMessage: return "delegates/TextMessage.qml"
					case MtxEvent.ImageMessage: return "delegates/ImageMessage.qml"
					case MtxEvent.VideoMessage: return "delegates/VideoMessage.qml"
					case MtxEvent.Redacted: return "delegates/Redacted.qml"
					default: return "delegates/placeholder.qml"
				}
				property variant eventData: model
			}


			Button {
				Layout.alignment: Qt.AlignRight | Qt.AlignTop
				id: replyButton
				flat: true
				Layout.preferredHeight: 16
				ToolTip.visible: hovered
				ToolTip.text: qsTr("Reply")

				// disable background, because we don't want a border on hover
				background: Item {
				}

				Image {
					id: replyButtonImg
					// Workaround, can't get icon.source working for now...
					anchors.fill: parent
					source: "qrc:/icons/icons/ui/mail-reply.png"
				}
				ColorOverlay {
					anchors.fill: replyButtonImg
					source: replyButtonImg
					color: replyButton.hovered ? colors.highlight : colors.buttonText
				}

				onClicked: chat.model.replyAction(model.id)
			}
			Button {
				Layout.alignment: Qt.AlignRight | Qt.AlignTop
				id: optionsButton
				flat: true
				Layout.preferredHeight: 16
				ToolTip.visible: hovered
				ToolTip.text: qsTr("Options")

				// disable background, because we don't want a border on hover
				background: Item {
				}

				Image {
					id: optionsButtonImg
					// Workaround, can't get icon.source working for now...
					anchors.fill: parent
					source: "qrc:/icons/icons/ui/vertical-ellipsis.png"
				}
				ColorOverlay {
					anchors.fill: optionsButtonImg
					source: optionsButtonImg
					color: optionsButton.hovered ? colors.highlight : colors.buttonText
				}

				onClicked: contextMenu.open()

				Menu {
					y: optionsButton.height
					id: contextMenu

					MenuItem {
						text: "Read receipts"
					}
					MenuItem {
						text: "Mark as read"
					}
					MenuItem {
						text: "View raw message"
						onTriggered: chat.model.viewRawMessage(model.id)
					}
					MenuItem {
						text: "Redact message"
					}
				}
			}

			Text {
				Layout.alignment: Qt.AlignRight | Qt.AlignTop
				text: model.timestamp.toLocaleTimeString("HH:mm")
				color: inactiveColors.text

				ToolTip.visible: ma.containsMouse
				ToolTip.text: Qt.formatDateTime(model.timestamp, Qt.DefaultLocaleLongDate)

				MouseArea{
					id: ma
					anchors.fill: parent
					hoverEnabled: true
				}
			}
		}

		section {
			property: "section"
			delegate: Column {
				topPadding: 4
				bottomPadding: 4
				spacing: 8

				width: parent.width

				Label {
					id: dateBubble
					anchors.horizontalCenter: parent.horizontalCenter
					visible: section.includes(" ")
					text: chat.model.formatDateSeparator(new Date(Number(section.split(" ")[1])))
					color: colors.windowText

					height: contentHeight * 1.2
					width: contentWidth * 1.2
					horizontalAlignment: Text.AlignHCenter
					background: Rectangle {
						radius: parent.height / 2
						color: colors.dark
					}
				}
				Row {
					height: userName.height
					spacing: 4
					Avatar {
						width: 48
						height: 48
						url: chat.model.avatarUrl(section.split(" ")[0]).replace("mxc://", "image://MxcImage/")
						displayName: chat.model.displayName(section.split(" ")[0])
					}

					Text { 
						id: userName
						text: chat.model.escapeEmoji(chat.model.displayName(section.split(" ")[0]))
						color: chat.model.userColor(section.split(" ")[0], colors.window)
						textFormat: Text.RichText
					}
				}
			}
		}
	}
}