import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Rectangle {
    id: homePage
    width: 320
    height: 170
    color: Theme.bgPrimary

    property var controller: null
    property string fontFamily: "Microsoft YaHei, 微软雅黑, sans-serif"
    property var rootRef: null

    signal videoSelected(string bvid)
    signal searchRequested()
    signal loginRequested()

    // ── 内容区状态 ──
    property int tabIndex: 0
    property bool isLoading: controller ? controller.isLoading : false
    property int recommendDebounceMs: 3500
    property double lastRecommendRefreshMs: 0

    function switchTab(index) {
        // index: 0=推荐, 1=排行, 2=搜索, 3=我的
        if (index === 2) {
            searchRequested()
            return
        }

        // 已登录时，点击“我的”直接进入 UserPage
        if (index === 3) {
            if (controller && controller.loggedIn) {
                loginRequested()
                return
            }
        }

        // 只有在“推荐”页内再次点击时才刷新
        if (index === 0 && tabIndex === 0 && controller) {
            var nowMs = Date.now();
            if (nowMs - lastRecommendRefreshMs < recommendDebounceMs) {
                controller.toastMessage("操作过快，请稍后再试")
                return
            }
            lastRecommendRefreshMs = nowMs
            controller.fetchPopular(1, 10)
            controller.toastMessage("已刷新推荐")
            return
        }

        if (tabIndex === index) return
        tabIndex = index

        if (index === 1 && controller) {
            var rm = controller.rankingModel()
            if (rm && rm.count === 0) controller.fetchRanking()
        }
    }

    // ── 内容区 (无标题栏，高度 = 170 - 26 = 144px) ──
    Item {
        id: contentArea
        anchors {
            top: parent.top
            bottom: tabBar.top
            left: parent.left
            right: parent.right
        }
        clip: true

        // ──── Tab 0: 推荐 ────
        Item {
            anchors.fill: parent
            visible: tabIndex === 0
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }

            ListView {
            id: popularList
            anchors.fill: parent
            anchors.margins: 4
            model: controller ? controller.popularModel() : null
            orientation: ListView.Horizontal
            spacing: 6
            clip: true

                // 性能优化
                cacheBuffer: 640
                displayMarginBeginning: 160
                displayMarginEnd: 160

                delegate: VideoCardCompact {
                    height: popularList.height
                    videoTitle: model.title || ""
                    coverUrl: model.pic || ""
                    upName: model.ownerName || ""
                    viewCount: model.views || ""
                    durationText: model.durationText || ""
                    bvid: model.bvid || ""
                    fontFamily: homePage.fontFamily
                    onClicked: homePage.videoSelected(bvid)
                }

                onAtXEndChanged: {
                    if (atXEnd && controller && !isLoading && popularList.count > 0) {
                        controller.fetchMorePopular()
                    }
                }

                // 空状态
                Text {
                    visible: popularList.count === 0 && !isLoading
                    text: "暂无推荐视频"
                    color: Theme.textTertiary
                    font.family: homePage.fontFamily
                    font.pixelSize: 12
                    anchors.centerIn: parent
                }
            }
        }

        // ──── Tab 1: 排行 ────
        Item {
            anchors.fill: parent
            visible: tabIndex === 1
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }

            ListView {
                id: rankingList
                anchors.fill: parent
                anchors.margins: 4
                model: controller ? controller.rankingModel() : null
                orientation: ListView.Horizontal
                spacing: 6
                clip: true

                cacheBuffer: 640
                displayMarginBeginning: 160
                displayMarginEnd: 160

                delegate: VideoCardCompact {
                    height: rankingList.height
                    videoTitle: model.title || ""
                    coverUrl: model.pic || ""
                    upName: model.ownerName || ""
                    viewCount: model.views || ""
                    durationText: model.durationText || ""
                    bvid: model.bvid || ""
                    rankIndex: index + 1
                    showRank: true
                    fontFamily: homePage.fontFamily
                    onClicked: homePage.videoSelected(bvid)
                }

                Text {
                    visible: rankingList.count === 0 && !isLoading
                    text: "排行榜加载中..."
                    color: Theme.textTertiary
                    font.family: homePage.fontFamily
                    font.pixelSize: 12
                    anchors.centerIn: parent
                }
            }
        }

        // ──── Tab 3: 我的 ────
        Item {
            anchors.fill: parent
            visible: tabIndex === 3
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }

            Row {
                anchors.centerIn: parent
                spacing: 20

                // 头像区
                Column {
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter

                    Rectangle {
                        width: 56
                        height: 56
                        radius: 28
                        color: Theme.bgTertiary
                        anchors.horizontalCenter: parent.horizontalCenter
                        border.color: controller && controller.loggedIn
                        ? Theme.primary : Theme.borderLight
                        border.width: 2

                        Image {
                            id: avatarImg
                            anchors.fill: parent
                            anchors.margins: 2
                            visible: controller && controller.loggedIn && source != ""
                            source: controller && controller.userAvatar ? controller.userAvatar : ""
                            sourceSize: Qt.size(112, 112)
                            cache: true
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop

                            layer.enabled: true
                            layer.smooth: true
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: !avatarImg.visible
                            text: controller && controller.loggedIn ? "👤" : "🔐"
                            font.pixelSize: 24
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -8
                            onClicked: {
                                if (controller && controller.loggedIn)
                                    homePage.loginRequested()
                            }
                        }
                    }

                    Text {
                        text: controller && controller.loggedIn
                        ? controller.userName : "点击登录"
                        color: controller && controller.loggedIn
                        ? Theme.textPrimary : Theme.primary
                        font.family: homePage.fontFamily
                        font.pixelSize: 12
                        font.bold: true
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }

                // 信息区（登录后显示）
                Column {
                    visible: controller && controller.loggedIn
                    spacing: 8
                    anchors.verticalCenter: parent.verticalCenter

                    Row {
                        spacing: 6
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: Theme.success
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "已登录"
                            color: Theme.success
                            font.family: homePage.fontFamily
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: "Lv" + (controller ? controller.level : 0)
                        color: Theme.primary
                        font.family: homePage.fontFamily
                        font.pixelSize: 14
                        font.bold: true
                    }

                    Text {
                        text: "硬币: " + (controller ? controller.coins : 0)
                        color: Theme.textSecondary
                        font.family: homePage.fontFamily
                        font.pixelSize: 10
                    }
                }
            }
        }
    }

    // ── 底部标签栏 (含搜索) ──
    Rectangle {
        id: tabBar
        width: parent.width
        height: 26
        color: Theme.bgSecondary
        anchors.bottom: parent.bottom
        z: 10

        // 顶部边线
        Rectangle {
            width: parent.width
            height: 1
            anchors.top: parent.top
            color: Theme.borderLight
        }

        Row {
            anchors.centerIn: parent
            spacing: 6

            Repeater {
                model: [
                    { label: "推荐", idx: 0 },
                    { label: "排行", idx: 1 },
                    { label: "搜索", idx: 2 },
                    { label: "我的", idx: 3 }
                ]

                Rectangle {
                    width: 56
                    height: 20
                    radius: 10
                    color: {
                        if (modelData.idx === 2) {
                            return tabMouseArea.pressed
                            ? Theme.withAlpha(Theme.primary, 0.2)
                            : Theme.bgTertiary
                        }
                        return (tabIndex === modelData.idx || (modelData.idx === 3 && tabIndex === 3))
                        ? Theme.withAlpha(Theme.primary, 0.15)
                        : "transparent"
                    }

                    Behavior on color { ColorAnimation { duration: 100 } }

                    Text {
                        text: modelData.label
                        color: {
                            if (modelData.idx === 2) return Theme.textSecondary
                                return (tabIndex === modelData.idx)
                                ? Theme.primary
                                : Theme.textSecondary
                        }
                        font.family: homePage.fontFamily
                        font.pixelSize: 11
                        font.bold: tabIndex === modelData.idx && modelData.idx !== 2
                        anchors.centerIn: parent

                        Behavior on color { ColorAnimation { duration: 100 } }
                    }

                    MouseArea {
                        id: tabMouseArea
                        anchors.fill: parent
                        anchors.margins: -4
                        onClicked: switchTab(modelData.idx)
                    }
                }
            }
        }
    }

    // ── 加载指示器 ──
    Rectangle {
        visible: isLoading
        anchors.centerIn: contentArea
        width: loadingRow.width + 16
        height: 22
        radius: 11
        color: Theme.withAlpha(Theme.bgSecondary, 0.95)
        border.color: Theme.borderLight
        border.width: 1

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
                            running: isLoading
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
                    font.family: homePage.fontFamily
                    font.pixelSize: 9
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }

    // 首页首次加载由 main.qml 统一触发，避免返回时重复刷新
    function popularContentX() {
        return popularList ? popularList.contentX : 0
    }

    function restorePopularContentX(x) {
        if (popularList) {
            Qt.callLater(function() { popularList.contentX = x; })
        }
    }

    Component.onCompleted: {
        if (rootRef && rootRef.restoreHomePopularOnShow && tabIndex === 0) {
            restorePopularContentX(rootRef.homePopularX)
            rootRef.restoreHomePopularOnShow = false
        }
    }
}
