import QtQuick 2.12
import BiliPlugin 1.0
import ".."

Rectangle {
    id: titleBar
    width: parent ? parent.width : 320
    height: Theme.titleBarHeight
    color: Theme.bgSecondary
    z: 10

    property string title: ""
    property bool showBack: true
    property bool showSearch: false

    signal backClicked()
    signal searchClicked()

    // ── 返回按钮 ──
    Rectangle {
        id: backBtn
        visible: showBack
        width: 44
        height: parent.height
        color: "transparent"
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
            id: backBtnCore
            anchors.centerIn: parent
            width: 38; height: 22
            radius: Theme.radiusMedium
            color: backArea.pressed
            ? Theme.withAlpha(Theme.primary, 0.2) : "transparent"

            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            Row {
                anchors.centerIn: parent
                spacing: 2

                Text {
                    text: "‹"
                    color: Theme.primary
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "返回"
                    color: Theme.primary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            MouseArea {
                id: backArea
                anchors.fill: parent
                onClicked: titleBar.backClicked()
            }
        }
    }

    // ── 标题 ──
    Text {
        text: titleBar.title
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontMedium
        font.bold: true
        anchors.centerIn: parent
        elide: Text.ElideRight
        width: parent.width - 110
        horizontalAlignment: Text.AlignHCenter
    }

    // ── 搜索按钮 ──
    Rectangle {
        id: searchBtn
        visible: showSearch
        width: 36
        height: parent.height
        color: "transparent"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
            anchors.centerIn: parent
            width: 28; height: 22
            radius: Theme.radiusMedium
            color: searchArea.pressed
            ? Theme.withAlpha(Theme.primary, 0.2) : "transparent"

            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            Text {
                text: "🔍"
                font.pixelSize: Theme.fontNormal
                anchors.centerIn: parent
            }
        }

        MouseArea {
            id: searchArea
            anchors.fill: parent
            onClicked: titleBar.searchClicked()
        }
    }

    // ── 底部分割线 + 微光效果 ──
    Rectangle {
        width: parent.width; height: 1
        anchors.bottom: parent.bottom
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.3; color: Theme.withAlpha(Theme.primary, 0.3) }
            GradientStop { position: 0.7; color: Theme.withAlpha(Theme.primary, 0.3) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
