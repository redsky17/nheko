import QtQuick 2.10
import QtQuick.Layouts 1.2
import QtQuick.Controls 2.5
import QtMultimedia 5.6
import im.nheko 1.0

ColumnLayout { 
    // duration = milliseconds
	function durationToString(duration) {
		function maybeZeroPrepend(time) {
			return (time < 10) ? "0" + time.toString() :
				time.toString()
		}
		var totalSeconds = Math.floor(duration / 1000)
		var seconds = totalSeconds % 60
		var minutes = (Math.floor(totalSeconds / 60)) % 60
		var hours = (Math.floor(totalSeconds / (60 * 24))) % 24
		// Always show minutes and don't prepend zero into the leftmost element
		var ss = maybeZeroPrepend(seconds)
		var mm = (hours > 0) ? maybeZeroPrepend(minutes) : minutes.toString()
		var hh = hours.toString()

		if (hours < 1)
			return mm + ":" + ss
		return hh + ":" + mm + ":" + ss
	}
	
    id: content
    Layout.maximumWidth: parent ? parent.width: undefined

    MediaPlayer {
        id: media
        onError: console.log(errorString)
        volume: volumeSlider.desiredVolume
    }

    Connections {
        property bool mediaCached: false

        id: mediaCachedObserver
        target: TimelineManager.timeline
        onMediaCached: {
            if (mxcUrl == model.data.url) {
                mediaCached = true
                media.source = "file://" + cacheUrl
                console.log("media loaded: " + mxcUrl + " at " + cacheUrl)
            }
            console.log("media cached: " + mxcUrl + " at " + cacheUrl)
        }
    }
      
    Rectangle {
        id: videoContainer
        visible: model.data.type == MtxEvent.VideoMessage
        //property double tempWidth: Math.min(parent ? parent.width : undefined, model.data.width < 1 ? 400 : /////model.data.width)
        property double tempWidth: (model.data.width < 1) ? 400 : model.data.width
        property double tempHeight: tempWidth * model.data.proportionalHeight

        property double divisor: model.isReply ? 4 : 2
        property bool tooHigh: tempHeight > timelineRoot.height / divisor

        Layout.maximumWidth: Layout.preferredWidth
        Layout.preferredHeight: tooHigh ? timelineRoot.height / divisor : tempHeight
        Layout.preferredWidth: tooHigh ? (timelineRoot.height / divisor) / model.data.proportionalHeight : tempWidth
        Image {
            anchors.fill: parent
            source: model.data.thumbnailUrl.replace("mxc://", "image://MxcImage/")
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            // Button and window colored overlay to cache media
            Rectangle {
                // Display over video controls
                z: videoOutput.z + 1
                visible: !mediaCachedObserver.mediaCached
                anchors.fill: parent
                color: colors.window
                opacity: 0.5
                Image {
                    property color buttonColor: (cacheVideoArea.containsMouse) ? colors.highlight :
                        colors.text

                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    source: "image://colorimage/:/icons/icons/ui/arrow-pointing-down.png?"+buttonColor						
                }
                MouseArea {
                    id: cacheVideoArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !mediaCachedObserver.mediaCached
                    onClicked: TimelineManager.timeline.cacheMedia(model.data.id)
                }
            }
            VideoOutput {
                id: videoOutput
                clip: true
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
                source: media
                // TODO: once we can use Qt 5.12, use HoverHandler
                MouseArea {
                    id: playerMouseArea
                    // Toggle play state on clicks
                    onClicked: {
                        if (controlRect.shouldShowControls &&
                            !controlRect.contains(mapToItem(controlRect, mouseX, mouseY))) {
                                (media.playbackState == MediaPlayer.PlayingState) ?
                                    media.pause() :
                                    media.play()
                        }
                    }
					Rectangle {
						id: controlRect
						property int controlHeight: 25
						property bool shouldShowControls: playerMouseArea.shouldShowControls ||
							volumeSliderRect.visible

						anchors.bottom: playerMouseArea.bottom
						// Window color with 128/255 alpha
						color: {
							var wc = colors.window
							return Qt.rgba(wc.r, wc.g, wc.b, 0.5)
						}
						height: 40
						width: playerMouseArea.width
						opacity: shouldShowControls ? 1 : 0
						// Fade controls in/out
						Behavior on opacity {
							OpacityAnimator {
								duration: 100
							}
						}

						RowLayout {
							anchors.fill: parent
							width: parent.width
							// Play/pause button
							Image {
								id: playbackStateImage
								fillMode: Image.PreserveAspectFit
								Layout.preferredHeight: controlRect.controlHeight
								Layout.alignment: Qt.AlignVCenter
								property color controlColor: (playbackStateArea.containsMouse) ?
									colors.highlight : colors.text

								source: (media.playbackState == MediaPlayer.PlayingState) ?
									"image://colorimage/:/icons/icons/ui/pause-symbol.png?"+controlColor :
									"image://colorimage/:/icons/icons/ui/play-sign.png?"+controlColor
								MouseArea {
									id: playbackStateArea
									
									anchors.fill: parent
									hoverEnabled: true
									onClicked: {
										(media.playbackState == MediaPlayer.PlayingState) ?
											media.pause() :
											media.play()
									}
								}
							}
							Label {
								text: (!mediaCachedObserver.mediaCached) ? "-/-" :
									durationToString(media.position) + "/" + durationToString(media.duration)
							}

							Slider {
								Layout.fillWidth: true
								Layout.minimumWidth: 50
								height: controlRect.controlHeight
								value: media.position
								onMoved: media.seek(value)
								from: 0
								to: media.duration
							}
							// Volume slider activator
							Image {
								property color controlColor: (volumeImageArea.containsMouse) ?
									colors.highlight : colors.text

								// TODO: add icons for different volume levels
								id: volumeImage
								source: (media.volume > 0 && !media.muted) ?
									"image://colorimage/:/icons/icons/ui/volume-up.png?"+ controlColor :
									"image://colorimage/:/icons/icons/ui/volume-off-indicator.png?"+ controlColor
								Layout.rightMargin: 5
								Layout.preferredHeight: controlRect.controlHeight
								fillMode: Image.PreserveAspectFit
								MouseArea {
									id: volumeImageArea	
									anchors.fill: parent
									hoverEnabled: true
									onClicked: media.muted = !media.muted
									onExited: volumeSliderHideTimer.start()
									onPositionChanged: volumeSliderHideTimer.start()
									// For hiding volume slider after a while
									Timer {
										id: volumeSliderHideTimer
										interval: 1500
										repeat: false
										running: false
									}
								}
								Rectangle {
									id: volumeSliderRect
									opacity: (visible) ? 1 : 0
									Behavior on opacity {
										OpacityAnimator {
											duration: 100
										}
									}
									// TODO: figure out a better way to put the slider popup above controlRect
									anchors.bottom: volumeImage.top
									anchors.bottomMargin: 10
									anchors.horizontalCenter: volumeImage.horizontalCenter
									color: {
										var wc = colors.window
										return Qt.rgba(wc.r, wc.g, wc.b, 0.5)
									}
									/* TODO: base width on the slider width (some issue with it not having a geometry
										when using the width here?) */
									width: volumeImage.width * 0.7
									radius: volumeSlider.width / 2
									height: controlRect.height * 2 //100
									visible: volumeImageArea.containsMouse ||
										volumeSliderHideTimer.running ||
										volumeSliderRectMouseArea.containsMouse
									Slider {
										// Desired value to avoid loop onMoved -> media.volume -> value -> onMoved...
										property real desiredVolume: 1
										
										// TODO: the slider is slightly off-center on the left for some reason...
										id: volumeSlider
										from: 0
										to: 1
										value: (media.muted) ? 0 :
											QtMultimedia.convertVolume(desiredVolume,
												QtMultimedia.LinearVolumeScale,
												QtMultimedia.LogarithmicVolumeScale)
										anchors.fill: parent
										anchors.bottomMargin: parent.height * 0.1
										anchors.topMargin: parent.height * 0.1
										anchors.horizontalCenter: parent.horizontalCenter
										orientation: Qt.Vertical
										onMoved: desiredVolume = QtMultimedia.convertVolume(value,
											QtMultimedia.LogarithmicVolumeScale,
											QtMultimedia.LinearVolumeScale)
										/* This would be better handled in 'media', but it has some issue with listening
											to this signal */
										onDesiredVolumeChanged: media.muted = !(desiredVolume > 0)
									}
									// Used for resetting the timer on mouse moves on volumeSliderRect
									MouseArea {
										id: volumeSliderRectMouseArea
										anchors.fill: parent
										hoverEnabled: true
										propagateComposedEvents: true
										onExited: volumeSliderHideTimer.start()

										onClicked: mouse.accepted = false
										onPressed: mouse.accepted = false
										onReleased: mouse.accepted = false
										onPressAndHold: mouse.accepted = false
										onPositionChanged: {
											mouse.accepted = false
											volumeSliderHideTimer.start()
										}
									}
								}
							}

						}
					}
                    // This breaks separation of concerns but this same thing doesn't work when called from controlRect...
                    property bool shouldShowControls: (containsMouse && controlHideTimer.running) ||
                        (media.playbackState != MediaPlayer.PlayingState) ||
                        controlRect.contains(mapToItem(controlRect, mouseX, mouseY))

                    // For hiding controls on stationary cursor
                    Timer {
                        id: controlHideTimer
                        interval: 1500 //ms
                        repeat: false
                    }

                    hoverEnabled: true
                    onPositionChanged: controlHideTimer.start()

                    x: videoOutput.contentRect.x
                    y: videoOutput.contentRect.y
                    width: videoOutput.contentRect.width
                    height: videoOutput.contentRect.height
                    propagateComposedEvents: true
                }
            }
        }
    }
    // Audio player
    // TODO: share code with the video player
    Rectangle {
		id: audioControlRect
		
		visible: model.data.type != MtxEvent.VideoMessage
		property int controlHeight: 25
		Layout.preferredHeight: 40
		RowLayout {
			anchors.fill: parent
			width: parent.width
			// Play/pause button
			Image {
				id: audioPlaybackStateImage
				fillMode: Image.PreserveAspectFit
				Layout.preferredHeight: controlRect.controlHeight
				Layout.alignment: Qt.AlignVCenter
				property color controlColor: (audioPlaybackStateArea.containsMouse) ?
					colors.highlight : colors.text

				source: {
                    if (!mediaCachedObserver.mediaCached)
                        return "image://colorimage/:/icons/icons/ui/arrow-pointing-down.png?"+controlColor
                    return (media.playbackState == MediaPlayer.PlayingState) ?
                        "image://colorimage/:/icons/icons/ui/pause-symbol.png?"+controlColor :
                        "image://colorimage/:/icons/icons/ui/play-sign.png?"+controlColor
                }
				MouseArea {
					id: audioPlaybackStateArea
					
					anchors.fill: parent
					hoverEnabled: true
					onClicked: {
                        if (!mediaCachedObserver.mediaCached) {
                            TimelineManager.timeline.cacheMedia(model.data.id)
                            return
                        }
						(media.playbackState == MediaPlayer.PlayingState) ?
							media.pause() :
							media.play()
					}
				}
			}
			Label {
				text: (!mediaCachedObserver.mediaCached) ? "-/-" :
					durationToString(media.position) + "/" + durationToString(media.duration)
			}

			Slider {
				Layout.fillWidth: true
				Layout.minimumWidth: 50
				height: controlRect.controlHeight
				value: media.position
				onMoved: media.seek(value)
				from: 0
				to: media.duration
			}
		}
	}
    
    Label {
        id: fileInfoLabel
        
        background: Rectangle {
            color: colors.base
        }
		Layout.fillWidth: true
		text: model.data.body + " [" + model.data.filesize + "]"
		textFormat: Text.PlainText
		elide: Text.ElideRight
		color: colors.text
	}
}
