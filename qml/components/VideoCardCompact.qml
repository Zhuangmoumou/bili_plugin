import QtQuick 2.12
import ".."

Item {
    id: card
    width: 105
    height: parent.height

    property string videoTitle: ""
    property string coverUrl: ""
    property string upName: ""
    property string viewCount: ""
    property string durationText: ""
    property string bvid: ""
    property int rankIndex: 0
    // 由外部显式控制是否显示选集角标
    property bool showCollection: false
    property bool showRank: false
    property string fontFamily: "Microsoft YaHei"
    property real fontScale: 1.0
    property real titleScale: 1.0
    property real subScale: 1.0
    property bool titleBold: true
    // 标题与UP信息之间的垂直间距（默认 2）
    property real infoSpacing: 2
    // 注意：该组件的文本在 Column 中布局，直接改子项 y 通常不会生效
    property real subYOffset: 0
    property string coverImageSource: ""

    signal clicked()

    function normalizedCoverSource(url) {
        if (!url) return ""
        var s = String(url)
        if (s.indexOf("data:image/") === 0 || s.indexOf("image://") === 0) return s
        return "image://bili/" + encodeURIComponent(s)
    }

    function scheduleCoverLoad() {
        var requested = coverUrl
        coverImageSource = ""
        if (!requested) return
        Qt.callLater(function() {
            if (coverUrl === requested) coverImageSource = normalizedCoverSource(requested)
        })
    }

    Component.onCompleted: scheduleCoverLoad()
    onCoverUrlChanged: scheduleCoverLoad()

    Rectangle {
        id: cardBg
        anchors.fill: parent
        radius: 6
        color: Theme.bgSecondary
        border.color: mouseArea.pressed ? Theme.primary : "transparent"
        border.width: 1

        Behavior on border.color { ColorAnimation { duration: 80 } }

        // 封面区 (高度约 65%)
        Rectangle {
            id: coverContainer
            width: parent.width - 4
            height: parent.height * 0.58
            anchors.top: parent.top
            anchors.topMargin: 2
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 4
            color: Theme.bgTertiary
            clip: true

            Image {
                id: coverImage
                anchors.fill: parent
                source: coverImageSource
                sourceSize: Qt.size(210, 140)
                cache: true
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                smooth: true
                mipmap: true

                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }

            // 占位
            Text {
                visible: coverImage.status !== Image.Ready
                text: "📺"
                font.pixelSize: 18
                opacity: 0.3
                anchors.centerIn: parent
            }

            // 时长
            Rectangle {
                visible: durationText.length > 0
                anchors { right: parent.right; bottom: parent.bottom; margins: 3 }
                width: durationLabel.width + 8
                height: 14
                radius: 3
                color: "#CC000000"

                Text {
                    id: durationLabel
                    text: durationText
                    color: "#FFFFFF"
                    font.family: card.fontFamily
                    font.pixelSize: 9 * card.fontScale
                    font.bold: true
                    anchors.centerIn: parent
                }
            }

            // 选集角标（多P视频）
            Rectangle {
                visible: showCollection
                anchors { left: parent.left; bottom: parent.bottom; leftMargin: 4; bottomMargin: 4 }
                width: collectionText.implicitWidth + 10
                height: 14
                radius: 6
                color: Qt.rgba(0, 0, 0, 0.58)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.18)
                z: 2

                Text {
                    id: collectionText
                    text: "选集"
                    color: "#F8FAFC"
                    font.family: card.fontFamily
                    font.pixelSize: 8
                    font.bold: true
                    anchors.centerIn: parent
                }
            }

            // 排名
            Rectangle {
                visible: showRank && rankIndex > 0
                anchors { left: parent.left; top: parent.top; margins: 3 }
                width: 16; height: 14
                radius: 3
                color: rankIndex <= 3 ? Theme.error : "#CC000000"

                Text {
                    text: rankIndex
                    color: "#FFFFFF"
                    font.family: card.fontFamily
                    font.pixelSize: 9
                    font.bold: true
                    anchors.centerIn: parent
                }
            }
        }

        // 信息区
        Column {
            anchors {
                top: coverContainer.bottom
                topMargin: 4
                left: parent.left
                right: parent.right
                leftMargin: 5
                rightMargin: 5
                bottom: parent.bottom
                bottomMargin: 3
            }
            spacing: infoSpacing

            // 标题
            Text {
                width: parent.width
                text: videoTitle
                color: Theme.textPrimary
                font.family: card.fontFamily
                font.pixelSize: 10 * card.fontScale * card.titleScale
                font.bold: titleBold
                maximumLineCount: 2
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                lineHeight: 1.15
            }

            // UP主 + 播放量
            Text {
                width: parent.width
                // 处于 Column 布局中，y 可能会被布局覆盖，保留该属性以兼容需要时的手动布局
                y: subYOffset
                text: upName + (viewCount ? " · " + viewCount : "")
                color: Theme.textTertiary
                font.family: card.fontFamily
                font.pixelSize: 8 * card.fontScale * card.subScale
                elide: Text.ElideRight
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            onClicked: card.clicked()
        }
    }
}
