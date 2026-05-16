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
    property bool upSeasonsRequested: false

    signal backClicked()
    signal videoSelected(string bvid)

    function clampScrollState() {
        Qt.callLater(function() {
            var maxY = Math.max(0, mainFlick.contentHeight - mainFlick.height)
            if (mainFlick.contentY > maxY) mainFlick.contentY = maxY
            if (mainFlick.contentY < 0) mainFlick.contentY = 0
        })
    }

    function resetScrollState() {
        Qt.callLater(function() {
            mainFlick.contentY = 0
            filterFlick.contentX = 0
            upVideoList.contentX = 0
            clampScrollState()
        })
    }

    function resetListState() {
        Qt.callLater(function() {
            upVideoList.contentX = 0
            clampScrollState()
        })
    }

    function refresh() {
        var midVal = Number(upMid)
        if (!controller || !midVal || midVal <= 0) return
        controller.fetchUpInfo(midVal)
    }

    function formatFanCount(value) {
        var n = Number(value || 0)
        if (n < 10000) return String(Math.floor(n))

        var wan = n / 10000.0
        var s = wan.toFixed(2)
        s = s.replace(/\.?0+$/, "")
        return s + "万"
    }

    onUpMidChanged: {
        upVideosRequested = false
        upSeasonsRequested = false
        var midVal = Number(upMid)
        if (controller && controller.upUserMid === midVal && controller.upSelectedSeasonId !== 0) {
            controller.selectUpSeason(0, "", false, 0)
            upVideosRequested = true
        }
        resetScrollState()
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
            if (!upVideosRequested) {
                upVideosRequested = true
                Qt.callLater(function() {
                    if (!controller || controller.upUserMid !== midVal) return
                    if (controller.upSelectedSeasonId === 0) {
                        controller.fetchUpVideos(midVal, 1, 20)
                    }
                })
            }
            if (!upSeasonsRequested) {
                upSeasonsRequested = true
                Qt.callLater(function() {
                    if (!controller || controller.upUserMid !== midVal) return
                    controller.fetchUpSeasons(midVal)
                })
            }
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
        contentHeight: Math.max(height, contentColumn.implicitHeight + Theme.spacingLarge * 2)
        onContentHeightChanged: upPage.clampScrollState()
        boundsBehavior: Flickable.StopAtBounds
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
                            color: controller && controller.upIsFollowing
                                   ? (followArea.pressed ? Theme.bgTertiary : Theme.bgSecondary)
                                   : (followArea.pressed ? Theme.primaryDark : Theme.primary)
                            border.width: controller && controller.upIsFollowing ? 1 : 0
                            border.color: controller && controller.upIsFollowing
                                          ? Theme.withAlpha(Theme.primary, 0.35)
                                          : "transparent"
                            width: followText.implicitWidth + 16

                            Text {
                                id: followText
                                anchors.centerIn: parent
                                text: controller && controller.upIsFollowing ? "已关注" : "关注"
                                color: controller && controller.upIsFollowing ? Theme.textSecondary : Theme.textOnPrimary
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
                        text: controller ? upPage.formatFanCount(controller.upUserFans) : "0"
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

            // 投稿视频 / 合集筛选
            Column {
                width: parent.width
                spacing: Theme.spacingSmall

                // 标题行：当前筛选名 + 视频数量提示
                Item {
                    width: parent.width
                    height: titleHeaderText.implicitHeight

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingLarge
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingSmall

                        // 装饰条
                        Rectangle {
                            width: 3
                            height: titleHeaderText.implicitHeight - 2
                            radius: 1.5
                            color: Theme.primary
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            id: titleHeaderText
                            text: controller && controller.upSelectedSeasonId !== 0
                                  ? (controller.upSelectedSeasonName || "合集")
                                  : "投稿视频"
                            color: Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontMedium
                            font.bold: true
                            elide: Text.ElideRight
                            // 限制最大宽度，避免过长合集名挤掉计数
                            // 这里 listCountText 宽度可变，取一个保守上限
                            width: Math.min(implicitWidth, upPage.width - 90)
                        }

                        Text {
                            id: listCountText
                            anchors.verticalCenter: parent.verticalCenter
                            visible: controller && (controller.upVideoTotal > 0 || (!!upVideoList.model && upVideoList.count > 0))
                            text: " · " + (controller.upVideoTotal > 0 ? controller.upVideoTotal : upVideoList.count) + " 个视频"
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }

                // 合集筛选条：水平滚动 chip 列表
                Item {
                    id: filterStrip
                    width: parent.width
                    height: 24

                    Flickable {
                        id: filterFlick
                        anchors.fill: parent
                        contentWidth: filterRow.width + Theme.spacingLarge * 2
                        contentHeight: filterStrip.height
                        flickableDirection: Flickable.HorizontalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        Row {
                            id: filterRow
                            x: Theme.spacingLarge
                            spacing: 6
                            anchors.verticalCenter: parent.verticalCenter

                            // “视频”全部 chip
                            Rectangle {
                                id: allChip
                                height: filterStrip.height
                                width: allChipText.implicitWidth + 26
                                radius: height / 2
                                property bool selected: !controller || controller.upSelectedSeasonId === 0
                                color: selected ? Theme.primary
                                                : (allChipArea.pressed ? Theme.bgTertiary : Theme.bgSecondary)
                                border.color: selected ? "transparent"
                                                       : Theme.withAlpha(Theme.primary, 0.25)
                                border.width: selected ? 0 : 1

                                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                                Text {
                                    id: allChipText
                                    anchors.centerIn: parent
                                    text: "视频"
                                    color: parent.selected ? Theme.textOnPrimary : Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    font.bold: parent.selected
                                }

                                MouseArea {
                                    id: allChipArea
                                    anchors.fill: parent
                                    onClicked: {
                                        if (!controller) return
                                        if (controller.upSelectedSeasonId !== 0) {
                                            controller.selectUpSeason(0, "", false, 0)
                                            upPage.resetListState()
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: controller ? controller.upSeasonModel() : null

                                delegate: Rectangle {
                                    id: seasonChip
                                    height: filterStrip.height
                                    width: Math.min(seasonChipText.implicitWidth + 26, 172)
                                    radius: height / 2
                                    property bool selected: controller && controller.upSelectedSeasonId === model.seasonId
                                    color: selected ? Theme.primary
                                                    : (seasonChipArea.pressed ? Theme.bgTertiary : Theme.bgSecondary)
                                    border.color: selected ? "transparent"
                                                           : Theme.withAlpha(Theme.primary, 0.25)
                                    border.width: selected ? 0 : 1

                                    Behavior on color { ColorAnimation { duration: Theme.animFast } }

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 4

                                        // 合集小图标：两层错位的小方块，暗示叠放/合集
                                        Item {
                                            width: 10; height: 10
                                            anchors.verticalCenter: parent.verticalCenter

                                            Rectangle {
                                                width: 7; height: 7
                                                radius: 1.5
                                                color: "transparent"
                                                border.color: seasonChip.selected ? Theme.textOnPrimary : Theme.textSecondary
                                                border.width: 1
                                                x: 0; y: 3
                                            }
                                            Rectangle {
                                                width: 7; height: 7
                                                radius: 1.5
                                                color: seasonChip.selected ? Theme.textOnPrimary : Theme.textSecondary
                                                x: 3; y: 0
                                            }
                                        }

                                        Text {
                                            id: seasonChipText
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: (model.name || "合集")
                                                  + (model.total > 0 ? "·" + model.total : "")
                                            color: seasonChip.selected ? Theme.textOnPrimary : Theme.textSecondary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSmall
                                            font.bold: seasonChip.selected
                                            elide: Text.ElideRight
                                            // chip 总宽 max 172，扣掉左右内边距与图标和间距 ≈ 140
                                            width: Math.min(implicitWidth, 140)
                                        }
                                    }

                                    MouseArea {
                                        id: seasonChipArea
                                        anchors.fill: parent
                                        onClicked: {
                                            if (!controller) return
                                            if (!seasonChip.selected) {
                                                controller.selectUpSeason(model.seasonId,
                                                                          model.name || "",
                                                                          model.isSeries === true,
                                                                          model.total || 0)
                                                upPage.resetListState()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                }

                Connections {
                    target: controller ? controller.upSeasonModel() : null
                    function onLoadingChanged() {
                        if (!target || target.loading) return
                        upPage.clampScrollState()
                    }
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

                Connections {
                    target: upVideoList.model
                    function onLoadingChanged() {
                        if (!target || target.loading) return
                        upPage.clampScrollState()
                    }
                }

                Text {
                    visible: upVideoList.count === 0 && controller && !controller.isLoading
                    text: controller && controller.upSelectedSeasonId !== 0 ? "该合集暂无视频" : "暂无投稿"
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
