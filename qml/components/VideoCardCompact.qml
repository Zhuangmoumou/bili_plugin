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
    // 稳妥显示：由外部显式控制是否显示合集
    property bool showCollection: false
    property bool showRank: false
    property string fontFamily: "Microsoft YaHei"

    signal clicked()

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
                source: coverUrl
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
                    font.pixelSize: 9
                    font.bold: true
                    anchors.centerIn: parent
                }
            }

            // 合集标识（多P视频）
            Rectangle {
                visible: showCollection
                anchors { left: parent.left; bottom: parent.bottom; leftMargin: 3; bottomMargin: 3 }
                width: collectionText.implicitWidth + 8
                height: 14
                radius: 3
                color: "#CC999999"
                z: 2

                Text {
                    id: collectionText
                    text: "合集"
                    color: "#222222"
                    font.family: card.fontFamily
                    font.pixelSize: 9
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
            spacing: 2

            // 标题
            Text {
                width: parent.width
                text: videoTitle
                color: Theme.textPrimary
                font.family: card.fontFamily
                font.pixelSize: 10
                font.bold: true
                maximumLineCount: 2
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                lineHeight: 1.15
            }

            // UP主 + 播放量
            Text {
                width: parent.width
                text: upName + (viewCount ? " · " + viewCount : "")
                color: Theme.textTertiary
                font.family: card.fontFamily
                font.pixelSize: 8
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
