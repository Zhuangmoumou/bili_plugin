import QtQuick 2.12
import BiliPlugin 1.0
import ".."

Rectangle {
    id: card
    width: 142
    height: 118
    radius: Theme.radiusLarge
    color: Theme.bgCard
    clip: true

    property string bvid: ""
    property string videoTitle: ""
    property string coverUrl: ""
    property string upName: ""
    property string viewCount: ""
    property string durationText: ""
    property bool showViewCount: true
    // 由外部显式控制是否显示选集角标
    property bool showCollection: false

    signal clicked(string bvid)

    // ── 按压动画 ──
    scale: cardArea.pressed ? 0.95 : 1.0
    Behavior on scale {
        NumberAnimation { duration: Theme.animFast; easing.type: Easing.OutCubic }
    }

    // ── 背景高亮 ──
    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Theme.withAlpha(Theme.primary, cardArea.pressed ? 0.1 : 0)
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        // ── 封面区域 ──
        Item {
            width: parent.width
            height: 74
            clip: true

            Rectangle {
                anchors.fill: parent
                color: Theme.bgTertiary

                // 顶部圆角遮罩
                radius: Theme.radiusLarge
                Rectangle {
                    width: parent.width
                    height: Theme.radiusLarge
                    anchors.bottom: parent.bottom
                    color: parent.color
                }

                Image {
                    id: coverImg
                    anchors.fill: parent
                    source: coverUrl
                    ? "image://bili/" + encodeURIComponent(coverUrl) : ""
                    sourceSize: Qt.size(parent.width * 2, parent.height * 2)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    cache: true
                    opacity: status === Image.Ready ? 1 : 0

                    Behavior on opacity {
                        NumberAnimation { duration: Theme.animNormal }
                    }
                    
                    // 错误处理
                    onStatusChanged: {
                        if (status === Image.Error) {
                            console.log("[VideoCard] Cover load error:", coverUrl);
                        }
                    }
                }

                // 加载占位
                Text {
                    anchors.centerIn: parent
                    text: coverImg.status === Image.Loading ? "⏳" : "🎬"
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontLarge
                    visible: coverImg.status !== Image.Ready
                    opacity: 0.5
                }
            }

            // 时长标签
            Rectangle {
                visible: durationText.length > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 4
                height: 13
                width: durationLabel.implicitWidth + 8
                radius: Theme.radiusSmall
                color: Qt.rgba(0, 0, 0, 0.78)

                Text {
                    id: durationLabel
                    anchors.centerIn: parent
                    text: durationText
                    color: "#FFFFFF"
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                }
            }

            // 选集角标（多P视频）
            Rectangle {
                visible: showCollection
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 4
                anchors.bottomMargin: 4
                height: 13
                width: collectionText.implicitWidth + 10
                radius: 6
                color: Qt.rgba(0, 0, 0, 0.58)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.18)
                z: 2

                Text {
                    id: collectionText
                    anchors.centerIn: parent
                    text: "选集"
                    color: "#F8FAFC"
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                    font.bold: true
                }
            }

            // 底部渐变
            Rectangle {
                width: parent.width
                height: 20
                anchors.bottom: parent.bottom
                z: 1
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 1.0; color: Theme.bgCard }
                }
            }
        }

        // ── 信息区域 ──
        Item {
            width: parent.width
            height: parent.height - 74

            Column {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingNormal
                anchors.rightMargin: Theme.spacingNormal
                anchors.topMargin: Theme.spacingTiny
                anchors.bottomMargin: Theme.spacingSmall
                spacing: Theme.spacingTiny

                // 标题
                Text {
                    width: parent.width
                    text: videoTitle
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 1.15
                }

                // UP 主 + 播放量
                Row {
                    width: parent.width
                    spacing: Theme.spacingSmall

                    Text {
                        text: upName
                        color: "#7A7A7A"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        elide: Text.ElideRight
                        width: parent.width * 0.55
                    }

                    Row {
                        visible: showViewCount && viewCount.length > 0
                        spacing: 2
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: "▶"
                            color: Theme.textTertiary
                            font.pixelSize: 5
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: viewCount
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTiny
                        }
                    }
                }
            }
        }
    }

    MouseArea {
        id: cardArea
        anchors.fill: parent
        onClicked: card.clicked(card.bvid)
    }
}
