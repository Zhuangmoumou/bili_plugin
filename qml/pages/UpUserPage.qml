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
    property bool refreshing: false

    signal backClicked()
    signal videoSelected(string bvid)

    function refresh() {
        var midVal = Number(upMid)
        if (!controller || !midVal || midVal <= 0) return
        controller.fetchUpInfo(midVal)
    }

    // 刷新结束：用 controller.isLoading 的变化来收口
    Connections {
        target: controller
        function onIsLoadingChanged() {
            if (!controller || controller.isLoading) return
            if (refreshing) {
                refreshing = false
                // 允许后续 onUpUserChanged 再次触发自动加载
                upVideosRequested = false
            }
        }
    }

    function refreshAll() {
        var midVal = Number(upMid)
        if (!controller || !midVal || midVal <= 0) return
        refreshing = true
        // 避免 onUpUserChanged 再触发一次 fetchUpVideos
        upVideosRequested = true
        controller.fetchUpInfo(midVal)
        controller.fetchUpVideos(midVal, 1, 20)
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
        id: mainFlick
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentHeight: contentColumn.height + Theme.spacingLarge
        clip: true

        // 下拉刷新触发
        onMovementEnded: {
            if (contentY < -28 && !refreshing) {
                refreshAll()
            }
        }

        // 下拉刷新提示（会在 contentY<0 时露出）
        Item {
            id: refreshHeader
            width: parent.width
            height: 24
            y: -24
            visible: mainFlick.contentY < 0 || refreshing

            Text {
                anchors.centerIn: parent
                text: refreshing ? "刷新中..." : "下拉刷新"
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
            }
        }

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

                    Row {
                        width: parent.width
                        spacing: Theme.spacingSmall

                        Text {
                            id: upNameText
                            width: parent.width - uidBadge.width - Theme.spacingSmall
                            text: controller ? controller.upUserName : ""
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        // UID 徽标
                        Rectangle {
                            id: uidBadge
                            height: 18
                            radius: Theme.radiusRound
                            color: Theme.withAlpha(Theme.primary, 0.12)
                            border.color: Theme.withAlpha(Theme.primary, 0.35)
                            border.width: 1
                            width: uidText.implicitWidth + 14

                            Text {
                                id: uidText
                                anchors.centerIn: parent
                                text: "UID " + (controller ? String(controller.upUserMid) : "0")
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                            }
                        }
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

                    // 注意：当列表内容不足以撑满宽度时，atXEnd 会一直为 true，
                    // 可能导致无限触发“加载更多”并表现为“循环同一列表”。
                    // 这里增加防抖/条件：仅当确实可横向滚动且未在加载时才触发。
                    onAtXEndChanged: {
                        if (!controller) return
                        if (!atXEnd) return
                        if (upVideoList.contentWidth <= upVideoList.width + 2) return
                        if (upVideoList.model && upVideoList.model.loading) return
                        if (upVideoList.model && upVideoList.model.hasMore === false) return
                        controller.fetchMoreUpVideos()
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

    // 加载中（同 HomePage：可取消）
    Rectangle {
        visible: controller && controller.isLoading && (!upVideoList || upVideoList.count === 0)
        anchors.centerIn: parent
        width: loadingRow.width + 16
        height: 22
        radius: 11
        color: Theme.withAlpha(Theme.bgSecondary, 0.95)
        border.color: Theme.borderLight
        border.width: 1
        z: 200

        Row {
            id: loadingRow
            anchors.centerIn: parent
            spacing: 3

            Repeater {
                model: 3
                Item {
                    width: 6
                    height: 12

                    Rectangle {
                        width: 5
                        height: 5
                        radius: 2.5
                        color: Theme.primary
                        anchors.centerIn: parent

                        SequentialAnimation on opacity {
                            running: upPage.visible && controller && controller.isLoading
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 120 }
                            NumberAnimation { to: 0.3; duration: 250 }
                            NumberAnimation { to: 1.0; duration: 250 }
                        }
                    }
                }
            }

            Item {
                width: childrenRect.width
                height: 12

                Text {
                    text: "加载中"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Rectangle {
                height: 16
                width: cancelTextItem.implicitWidth + 10
                radius: 8
                color: cancelArea.pressed
                       ? Theme.withAlpha(Theme.primary, 0.18)
                       : Theme.withAlpha(Theme.primary, 0.08)
                border.color: Theme.withAlpha(Theme.primary, 0.25)
                border.width: 1
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: cancelTextItem
                    anchors.centerIn: parent
                    text: "取消"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: 8
                }

                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    onClicked: {
                        if (controller) controller.cancelAll();
                    }
                }
            }
        }
    }
}
