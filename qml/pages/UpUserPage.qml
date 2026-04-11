import QtQuick 2.12
import QtGraphicalEffects 1.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Rectangle {
    id: upPage
    width: 320
    height: 170
    color: Theme.bgPrimary

    property var controller: null
    property var upMid: 0
    property bool upVideosRequested: false

    signal backClicked()
    signal videoSelected(string bvid)

    function refresh() {
        var midVal = Number(upMid)
        if (!controller || !midVal || midVal <= 0) return
        controller.fetchUpInfo(midVal)
    }

    onUpMidChanged: {
        upVideosRequested = false
        refresh()
    }

    onVisibleChanged: {
        if (visible) refresh()
    }

    Connections {
        target: controller
        function onUpUserChanged() {
            var midVal = Number(upMid)
            if (!controller || !midVal || controller.upUserMid !== midVal) return
            if (upVideosRequested) return
            upVideosRequested = true
            Qt.callLater(function() {
                if (!controller || controller.upUserMid !== midVal) return
                controller.fetchUpVideos(midVal, 1, 20)
            })
        }
    }

    Components.TitleBar {
        id: titleBar
        title: "UP 主页"
        showBack: true
        anchors.top: parent.top
        onBackClicked: upPage.backClicked()
    }

    Flickable {
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentHeight: contentColumn.height + Theme.spacingLarge
        clip: true

        Column {
            id: contentColumn
            width: parent.width
            spacing: Theme.spacingLarge
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingLarge

            // 头像 + 信息
            Row {
                id: headerRow
                width: parent.width - Theme.spacingLarge * 2
                height: 64
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacingLarge

                Rectangle {
                    id: avatarWrap
                    width: 64
                    height: 64
                    radius: 32
                    color: Theme.bgTertiary
                    border.color: Theme.primary
                    border.width: 2

                    Image {
                        id: avatarImage
                        anchors.fill: parent
                        anchors.margins: 2
                        source: controller && controller.upUserFace
                            ? "image://bili/" + encodeURIComponent(controller.upUserFace)
                            : ""
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        mipmap: true
                        asynchronous: true
                        visible: false
                    }

                    OpacityMask {
                        anchors.fill: avatarImage
                        source: avatarImage
                        maskSource: Rectangle {
                            width: avatarImage.width
                            height: avatarImage.height
                            radius: Math.min(width, height) / 2
                        }
                    }
                }

                Column {
                    id: infoColumn
                    width: parent.width - 96
                    spacing: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: controller ? controller.upUserName : ""
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMedium
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Row {
                        spacing: Theme.spacingSmall

                        Rectangle {
                            width: 40
                            height: 18
                            radius: Theme.radiusRound
                            color: Theme.primary

                            Text {
                                anchors.centerIn: parent
                                text: "LV" + (controller ? controller.upUserLevel : 0)
                                color: Theme.textOnPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                            }
                        }

                        Rectangle {
                            height: 18
                            radius: Theme.radiusRound
                            color: followArea.pressed ? Theme.primaryDark : Theme.primary
                            width: followText.implicitWidth + 16

                            Text {
                                id: followText
                                anchors.centerIn: parent
                                text: controller && controller.upIsFollowing ? "已关注" : "关注"
                                color: Theme.textOnPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                            }

                            MouseArea {
                                id: followArea
                                anchors.fill: parent
                                onClicked: {
                                    if (controller) controller.toggleUpFollow()
                                }
                            }
                        }
                    }
                }
            }

            // 个人签名（放在头像下面）
            Rectangle {
                width: parent.width - Theme.spacingLarge * 2
                anchors.horizontalCenter: parent.horizontalCenter
                radius: Theme.radiusLarge
                color: Theme.bgSecondary
                border.color: Theme.withAlpha(Theme.primary, 0.15)
                border.width: 1
                height: signText.implicitHeight + Theme.spacingMedium * 2

                Text {
                    id: signText
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    text: controller && controller.upUserSign !== "" ? controller.upUserSign : "这个人很懒，什么都没写~"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
            }

            // 粉丝/关注
            Row {
                width: parent.width - Theme.spacingLarge * 2
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacingSmall

                readonly property real colW: (width - Theme.spacingSmall) / 2

                Column {
                    width: parent.colW
                    spacing: 4

                    Text {
                        width: parent.width
                        text: controller ? String(controller.upUserFans) : "0"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMedium
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        width: parent.width
                        text: "粉丝"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                Column {
                    width: parent.colW
                    spacing: 4

                    Text {
                        width: parent.width
                        text: controller ? String(controller.upUserFollowing) : "0"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontMedium
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        width: parent.width
                        text: "关注"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            // 投稿视频
            Column {
                width: parent.width
                spacing: Theme.spacingSmall

                Text {
                    text: "投稿视频"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMedium
                    font.bold: true
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingLarge
                }

                ListView {
                    id: upVideoList
                    width: parent.width
                    height: 135
                    orientation: ListView.Horizontal
                    spacing: 6
                    clip: true
                    model: controller ? controller.upVideoModel() : null
                    leftMargin: 4
                    rightMargin: 4

                    onAtXEndChanged: {
                        if (atXEnd && controller) controller.fetchMoreUpVideos()
                    }

                    delegate: Components.VideoCardCompact {
                        height: upVideoList.height
                        videoTitle: model.title || ""
                        coverUrl: model.pic || ""
                        upName: model.ownerName || ""
                        viewCount: model.views || ""
                        durationText: model.durationText || ""
                        bvid: model.bvid || ""
                        showCollection: model.partCount > 1
                        fontFamily: Theme.fontFamily
                        titleScale: 0.9
                        subScale: 0.85
                        onClicked: upPage.videoSelected(bvid)
                    }
                }

                Text {
                    visible: upVideoList.count === 0 && controller && !controller.isLoading
                    text: "暂无投稿"
                    color: Theme.textTertiary
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: upVideoList.bottom
                    anchors.topMargin: 6
                }
            }
        }
    }
}
