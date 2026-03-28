import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Rectangle {
    id: userPage
    width: 320
    height: 170
    color: Theme.bgPrimary

    property var controller: null
    signal backClicked()
    signal videoSelected(string bvid)

    // 0=个人中心, 1=收藏夹列表, 2=收藏夹详情, 3=最近观看
    property int favView: 0
    property string currentFavTitle: ""
    property int currentFavId: 0

    function openFavorites() {
        favView = 1
        if (controller) controller.fetchFavoriteFolders()
    }

    function openFavoriteDetail(fid, title) {
        currentFavId = fid
        currentFavTitle = title
        favView = 2
        if (controller) controller.fetchFavoriteItems(fid, 1, 20)
    }

    function backInternal() {
        if (favView === 2) {
            favView = 1
            return
        }
        if (favView === 1) {
            favView = 0
            return
        }
        if (favView === 3) {
            favView = 0
            return
        }
        backClicked()
    }

    Component.onCompleted: {
        console.log("[UserPage] Created");
    }

    Component.onDestruction: {
        console.log("[UserPage] Destroyed, cleaning up");
        // 页面销毁时清空图片源，防止后台线程继续访问
        if (qrcodeImg) qrcodeImg.source = "";
        // 取消轮询
        if (qrcodePollTimer) qrcodePollTimer.running = false;
        // 注意：如果已登录成功，不要取消 checkLoginStatus() 请求
        // 让用户信息能够正常加载并保存
    }

    Components.TitleBar {
        id: titleBar
        title: {
            if (!controller || !controller.loggedIn) return "扫码登录"
            if (favView === 1) return "我的收藏夹"
            if (favView === 2) return currentFavTitle.length > 0 ? currentFavTitle : "收藏夹"
            if (favView === 3) return "最近观看"
            return "个人中心"
        }
        showBack: true
        anchors.top: parent.top
        onBackClicked: userPage.backInternal()
    }

    // ====== 未登录：二维码登录 ======
    Item {
        id: loginArea
        visible: !controller || !controller.loggedIn
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Row {
            anchors.centerIn: parent
            spacing: Theme.spacingLarge

            // ── 二维码区域（放大！）──
            Rectangle {
                width: 110; height: 110
                radius: Theme.radiusLarge
                color: "#FFFFFF"
                anchors.verticalCenter: parent.verticalCenter

                // 内部二维码
                Rectangle {
                    id: qrContainer
                    anchors.fill: parent
                    anchors.margins: 6
                    radius: Theme.radiusMedium
                    color: "#FFFFFF"
                    clip: true

                    Image {
                        id: qrcodeImg
                        anchors.fill: parent
                        // base64 data URL 直接使用，普通 URL 通过 image://bili/ 协议
                        source: {
                            if (!controller || !controller.qrcodeUrl) return ""
                            var url = controller.qrcodeUrl
                            // 如果是 base64 data URL，直接使用（添加时间戳防止缓存）
                            if (url.startsWith("data:image/")) {
                                return url + (controller.qrcodeKey ? ("&t=" + controller.qrcodeKey) : "")
                            }
                            // 普通图片 URL 通过 image://bili/ 协议
                            return "image://bili/" + encodeURIComponent(url)
                                + (controller.qrcodeKey ? ("?t=" + controller.qrcodeKey) : "")
                        }
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false  // 禁用 QML 缓存，让图片提供者完全控制
                        visible: controller ? controller.qrcodeUrl !== "" : false
                        opacity: visible ? 1 : 0

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.animNormal }
                        }

                        onStatusChanged: {
                            if (status === Image.Error) {
                                console.log("[QRCode] Image load error, status:", status);
                            } else if (status === Image.Ready) {
                                console.log("[QRCode] Image loaded successfully");
                            }
                        }
                    }

                    // 未获取时的占位
                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.spacingSmall
                        visible: !qrcodeImg.visible

                        Text {
                            text: "📱"
                            font.pixelSize: 22
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Text {
                            text: "点击获取\n二维码"
                            color: "#555555"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (controller && !controller.qrcodeUrl) {
                                controller.generateQrcode();
                            }
                        }
                    }
                }

                // 蓝色边框装饰
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    border.color: Theme.withAlpha(Theme.primary, 0.5)
                    border.width: 2
                }

                // 角标
                Rectangle {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: -4
                    anchors.rightMargin: -4
                    width: 20; height: 20
                    radius: Theme.radiusRound
                    color: Theme.primary
                    z: 2

                    Text {
                        anchors.centerIn: parent
                        text: "B"
                        color: Theme.textOnPrimary
                        font.pixelSize: Theme.fontNormal
                        font.bold: true
                    }
                }
            }

            // ── 说明文字 ──
            Column {
                width: 130
                spacing: Theme.spacingMedium
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    text: "使用 B 站 APP"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontMedium
                    font.bold: true
                }

                Column {
                    spacing: Theme.spacingSmall

                    Row {
                        spacing: Theme.spacingSmall
                        Rectangle {
                            width: 16; height: 16
                            radius: Theme.radiusRound
                            color: Theme.withAlpha(Theme.primary, 0.2)
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "1"
                                color: Theme.primary
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                            }
                        }
                        Text {
                            text: "打开 B 站 APP"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Row {
                        spacing: Theme.spacingSmall
                        Rectangle {
                            width: 16; height: 16
                            radius: Theme.radiusRound
                            color: Theme.withAlpha(Theme.primary, 0.2)
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "2"
                                color: Theme.primary
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                            }
                        }
                        Text {
                            text: "点击左上角扫一扫"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Row {
                        spacing: Theme.spacingSmall
                        Rectangle {
                            width: 16; height: 16
                            radius: Theme.radiusRound
                            color: Theme.withAlpha(Theme.primary, 0.2)
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "3"
                                color: Theme.primary
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                            }
                        }
                        Text {
                            text: "扫描左侧二维码"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                // 刷新按钮
                Rectangle {
                    width: 80; height: Theme.buttonHeight
                    radius: Theme.radiusRound
                    color: refreshArea.pressed ? Theme.primaryDark : Theme.primary
                    visible: controller ? controller.qrcodeUrl !== "" : false

                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    scale: refreshArea.pressed ? 0.93 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.animFast } }

                    Text {
                        anchors.centerIn: parent
                        text: "🔄 刷新二维码"
                        color: Theme.textOnPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }

                    MouseArea {
                        id: refreshArea
                        anchors.fill: parent
                        onClicked: {
                            if (controller) controller.generateQrcode();
                        }
                    }
                }
            }
        }

        // 轮询定时器
        Timer {
            id: qrcodePollTimer
            interval: 3000
            repeat: true
            running: controller
            ? (controller.qrcodeUrl !== "" && !controller.loggedIn) : false
            onTriggered: {
                if (controller) controller.pollQrcode();
            }
        }
    }

    // ====== 已登录：用户信息 / 收藏夹 ======
    Item {
        visible: controller ? controller.loggedIn : false
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        // ── 个人中心 ──
        Item {
            id: profileView
            anchors.fill: parent
            visible: favView === 0

            Flickable {
                anchors.fill: parent
                anchors.margins: Theme.spacingLarge
                contentHeight: contentColumn.height
                clip: true

                Column {
                    id: contentColumn
                    width: parent.width - Theme.spacingLarge * 2
                    spacing: Theme.spacingLarge
                    anchors.horizontalCenter: parent.horizontalCenter

                    // 头像和用户名
                    Row {
                        width: parent.width
                        spacing: Theme.spacingLarge
                        anchors.horizontalCenter: parent.horizontalCenter

                        // 头像
                        Rectangle {
                            width: 60; height: 60
                            radius: Theme.radiusRound
                            color: Theme.bgTertiary
                            border.color: Theme.primary
                            border.width: 2
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: 2
                                source: controller && controller.userFace
                                ? "image://bili/" + encodeURIComponent(controller.userFace) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }

                        Column {
                            spacing: Theme.spacingSmall
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                text: controller ? controller.userName : ""
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMedium
                                font.bold: true
                            }

                            Row {
                                spacing: Theme.spacingSmall

                                Rectangle {
                                    width: 10; height: 10
                                    radius: 5
                                    color: Theme.success
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Text {
                                    text: "已登录"
                                    color: Theme.success
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            // 等级和经验
                            Row {
                                spacing: Theme.spacingSmall

                                Rectangle {
                                    width: 40; height: 20
                                    radius: Theme.radiusRound
                                    color: Theme.primary
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        anchors.centerIn: parent
                                        text: "LV" + (controller ? controller.userLevel : 0)
                                        color: Theme.textOnPrimary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
                                        font.bold: true
                                    }
                                }

                                Item {
                                    width: 108; height: 20
                                    anchors.verticalCenter: parent.verticalCenter

                                    Rectangle {
                                        width: parent.width
                                        height: 5
                                        radius: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: Theme.withAlpha(Theme.bgTertiary, 0.9)
                                        clip: true

                                        Rectangle {
                                            width: parent.width * (controller ? controller.userExpProgress : 0)
                                            height: parent.height
                                            radius: parent.radius
                                            color: Theme.primary
                                        }
                                    }

                                    Text {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.bottom: parent.top
                                        anchors.bottomMargin: 2
                                        text: controller ? (String(controller.userExp) + "/" + String(controller.userExpNext)) : "0/0"
                                        color: Theme.textSecondary
                                        font.family: Theme.fontFamily
                                        //font.pixelSize: Theme.fontTiny
                                        font.pixelSize: 8
                                        font.bold: true
                                    }
                                }
                            }

                            // VIP 标签
                            Rectangle {
                                width: 60; height: 20
                                radius: Theme.radiusRound
                                color: controller && controller.userIsVip ? "#FB7299" : Theme.bgTertiary
                                visible: controller && (controller.userIsVip || controller.userVipLabel !== "")

                                Text {
                                    anchors.centerIn: parent
                                    text: controller && controller.userVipLabel !== "" ? controller.userVipLabel : "大会员"
                                    color: controller && controller.userIsVip ? Theme.textOnPrimary : Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                }
                            }
                        }
                    }

                    // 签名
                    Text {
                        text: controller && controller.userSign !== "" ? controller.userSign : "这个人很懒，什么都没写~"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }

                    // 统计数据
                    Row {
                        width: parent.width
                        spacing: Theme.spacingSmall
                        anchors.horizontalCenter: parent.horizontalCenter

                        readonly property real colW: (width - Theme.spacingSmall * 2) / 3

                        // 粉丝
                        Column {
                            width: parent.colW
                            spacing: 4

                            Text {
                                width: parent.width
                                text: controller ? String(controller.userFans) : "0"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMedium
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
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

                        // 关注
                        Column {
                            width: parent.colW
                            spacing: 4

                            Text {
                                width: parent.width
                                text: controller ? String(controller.userFollowing) : "0"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMedium
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
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

                        // 硬币
                        Column {
                            width: parent.colW
                            spacing: 4

                            Text {
                                width: parent.width
                                text: controller ? String(controller.userCoins) : "0"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontMedium
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: "硬币"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    // 快捷入口：收藏夹 / 最近观看
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.spacingLarge

                        Column {
                            spacing: 6

                            Rectangle {
                                width: 36
                                height: 36
                                radius: 18
                                color: favEntryArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.withAlpha(Theme.primary, 0.12)
                                border.color: Theme.withAlpha(Theme.primary, 0.35)
                                border.width: 1
                                anchors.horizontalCenter: parent.horizontalCenter

                                Canvas {
                                    anchors.centerIn: parent
                                    width: 16
                                    height: 16
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        ctx.fillStyle = Theme.primary
                                        var cx = width / 2
                                        var cy = height / 2
                                        var outerR = 8
                                        var innerR = 3.4
                                        ctx.beginPath()
                                        for (var i = 0; i < 5; i++) {
                                            var outerAngle = (i * 72 - 90) * Math.PI / 180
                                            var innerAngle = ((i * 72) + 36 - 90) * Math.PI / 180
                                            var ox = cx + outerR * Math.cos(outerAngle)
                                            var oy = cy + outerR * Math.sin(outerAngle)
                                            var ix = cx + innerR * Math.cos(innerAngle)
                                            var iy = cy + innerR * Math.sin(innerAngle)
                                            if (i === 0) ctx.moveTo(ox, oy)
                                            else ctx.lineTo(ox, oy)
                                            ctx.lineTo(ix, iy)
                                        }
                                        ctx.closePath()
                                        ctx.fill()
                                    }
                                }

                                MouseArea {
                                    id: favEntryArea
                                    anchors.fill: parent
                                    onClicked: userPage.openFavorites()
                                }
                            }

                            Text {
                                text: "收藏夹"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        Column {
                            spacing: 6

                            Rectangle {
                                width: 36
                                height: 36
                                radius: 18
                                color: historyEntryArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.withAlpha(Theme.primary, 0.12)
                                border.color: Theme.withAlpha(Theme.primary, 0.35)
                                border.width: 1
                                anchors.horizontalCenter: parent.horizontalCenter

                                Canvas {
                                    anchors.centerIn: parent
                                    width: 16
                                    height: 16
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        ctx.strokeStyle = Theme.primary
                                        ctx.lineWidth = 1.8
                                        ctx.lineCap = "round"
                                        ctx.beginPath()
                                        ctx.arc(9, 9, 6.5, 0, Math.PI * 2)
                                        ctx.stroke()
                                        ctx.beginPath()
                                        ctx.moveTo(9, 9)
                                        ctx.lineTo(9, 5.5)
                                        ctx.moveTo(9, 9)
                                        ctx.lineTo(12, 10.5)
                                        ctx.stroke()
                                    }
                                }

                                MouseArea {
                                    id: historyEntryArea
                                    anchors.fill: parent
                                    onClicked: {
                                        favView = 3
                                        if (controller) controller.fetchRecentHistory()
                                    }
                                }
                            }

                            Text {
                                text: "最近观看"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }
                    }

                    // 退出登录按钮
                    Rectangle {
                        width: 120; height: Theme.buttonHeight
                        radius: Theme.radiusRound
                        color: "transparent"
                        border.color: Theme.withAlpha(Theme.error, 0.5)
                        border.width: 1
                        anchors.horizontalCenter: parent.horizontalCenter

                        Behavior on color { ColorAnimation { duration: Theme.animFast } }

                        Text {
                            anchors.centerIn: parent
                            text: "退出登录"
                            color: Theme.withAlpha(Theme.error, 0.8)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (controller) controller.logout();
                            }
                            onPressed: parent.color = Theme.withAlpha(Theme.error, 0.1)
                            onReleased: parent.color = "transparent"
                        }
                    }
                }
            }
        }

        // ── 收藏夹列表 ──
        Item {
            id: favListView
            anchors.fill: parent
            visible: favView === 1

            ListView {
                id: favList
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                model: controller ? controller.favoriteFolderModel() : null
                spacing: Theme.spacingSmall
                clip: true

                delegate: Rectangle {
                    width: parent.width
                    height: 54
                    radius: Theme.radiusMedium
                    color: Theme.bgSecondary
                    border.color: Theme.withAlpha(Theme.primary, 0.15)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Rectangle {
                            width: 72
                            height: parent.height - 2
                            radius: Theme.radiusSmall
                            color: Theme.bgTertiary
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: model.cover ? "image://bili/" + encodeURIComponent(model.cover) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }

                        Column {
                            width: parent.width - 90
                            spacing: 4
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                text: model.title || ""
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "共" + (model.mediaCount || 0) + "个视频"
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: userPage.openFavoriteDetail(model.id, model.title)
                    }
                }
            }

            Text {
                visible: favList.count === 0 && controller && !controller.isLoading
                text: "暂无收藏夹"
                color: Theme.textTertiary
                anchors.centerIn: parent
            }

            Components.LoadingIndicator {
                anchors.centerIn: parent
                running: controller ? controller.isLoading : false
            }
        }

        // ── 最近观看 ──
        Item {
            id: recentHistoryView
            anchors.fill: parent
            visible: favView === 3

            ListView {
                id: recentList
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                model: controller ? controller.recentHistoryModel() : null
                orientation: ListView.Horizontal
                spacing: Theme.spacingMedium
                clip: true

                delegate: Components.VideoCard {
                    height: recentList.height
                    videoTitle: model.title || ""
                    coverUrl: model.pic || ""
                    upName: model.ownerName || ""
                    viewCount: ""
                    showViewCount: false
                    durationText: model.durationText || ""
                    bvid: model.bvid || ""
                    onClicked: userPage.videoSelected(bvid)
                }
            }

            Text {
                visible: recentList.count === 0
                text: "暂无最近观看"
                color: Theme.textTertiary
                anchors.centerIn: parent
            }
        }

        // ── 收藏夹详情 ──
        Item {
            id: favDetailView
            anchors.fill: parent
            visible: favView === 2

            ListView {
                id: favItems
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                model: controller ? controller.favoriteItemModel() : null
                orientation: ListView.Horizontal
                spacing: Theme.spacingMedium
                clip: true

                delegate: Components.VideoCard {
                    height: favItems.height
                    videoTitle: model.title || ""
                    coverUrl: model.pic || ""
                    upName: model.ownerName || ""
                    viewCount: model.views || ""
                    durationText: model.durationText || ""
                    bvid: model.bvid || ""
                    showCollection: model.partCount > 1
                    onClicked: userPage.videoSelected(bvid)
                }

                onAtXEndChanged: {
                    if (atXEnd && controller) controller.fetchMoreFavoriteItems()
                }
            }

            Text {
                visible: favItems.count === 0 && controller && !controller.isLoading
                text: "收藏夹为空"
                color: Theme.textTertiary
                anchors.centerIn: parent
            }

            Components.LoadingIndicator {
                anchors.centerIn: parent
                running: controller ? controller.isLoading : false
            }
        }
    }
}
