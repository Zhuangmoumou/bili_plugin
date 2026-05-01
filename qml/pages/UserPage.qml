import QtQuick 2.12
import QtGraphicalEffects 1.12
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

    // 0=个人中心, 1=收藏夹列表, 2=收藏夹详情, 3=最近观看, 4=设置, 5=字幕设置, 6=稍后再看
    property int favView: 0
    property string currentFavTitle: ""
    property int currentFavId: 0
    property real recentHistoryContentX: 0
    property real watchLaterContentX: 0

    function openFavorites() {
        favView = 1
        // 延后到下一帧触发，避免切换视图瞬间阻塞 UI
        if (controller) Qt.callLater(function() { controller.fetchFavoriteFolders() })
    }

    function openFavoriteDetail(fid, title) {
        currentFavId = fid
        currentFavTitle = title
        favView = 2
        // 延后到下一帧触发，避免进入详情时 UI 卡顿
        if (controller) Qt.callLater(function() { controller.fetchFavoriteItems(fid, 1, 20) })
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
        if (favView === 6) {
            favView = 0
            return
        }
        if (favView === 4) {
            favView = 0
            return
        }
        if (favView === 5) {
            favView = 4
            return
        }
        backClicked()
    }

    Component.onCompleted: {
        console.log("[UserPage] Created");
    }

    onVisibleChanged: {
        if (visible && controller && controller.loggedIn) {
            controller.refreshUserInfo();
            Qt.callLater(function() {
                if (recentLoader.item && recentLoader.item.restorePosition) {
                    recentLoader.item.restorePosition()
                }
                if (watchLaterLoader.item && watchLaterLoader.item.restorePosition) {
                    watchLaterLoader.item.restorePosition()
                }
            })
        }
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
            if (favView === 6) return "稍后再看"
            if (favView === 4) return "设置"
            if (favView === 5) return "字幕设置"
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

                    Row {
                       spacing: Theme.spacingSmall

                        Rectangle {
                            width: 16
                            height: 16
                            radius: Theme.radiusRound
                            color: Theme.withAlpha(Theme.primary, 0.2)
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: "4"
                                color: Theme.primary
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                            }
                        }

                        Text {
                            text: "或进行短信登录"
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

                            Image {
                                id: userAvatarImage
                                anchors.fill: parent
                                anchors.margins: 2
                                source: controller && controller.userFace
                                ? "image://bili/" + encodeURIComponent(controller.userFace) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                visible: false
                            }

                            OpacityMask {
                                anchors.fill: userAvatarImage
                                source: userAvatarImage
                                maskSource: Rectangle {
                                    width: userAvatarImage.width
                                    height: userAvatarImage.height
                                    radius: Math.min(width, height) / 2
                                }
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

                    // 快捷入口：收藏夹 / 稍后再看 / 最近观看 / 设置
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
                                        if (controller) Qt.callLater(function() { controller.fetchRecentHistory() })
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

                        Column {
                            spacing: 6

                            Rectangle {
                                width: 36
                                height: 36
                                radius: 18
                                color: watchLaterEntryArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.withAlpha(Theme.primary, 0.12)
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
                                        ctx.lineWidth = 1.6
                                        ctx.lineCap = "round"
                                        ctx.beginPath()
                                        ctx.arc(8, 8, 6, 0, Math.PI * 2)
                                        ctx.stroke()
                                        ctx.beginPath()
                                        ctx.moveTo(8, 8)
                                        ctx.lineTo(8, 4.5)
                                        ctx.moveTo(8, 8)
                                        ctx.lineTo(11.2, 9.4)
                                        ctx.stroke()
                                        ctx.beginPath()
                                        ctx.moveTo(12.5, 3.5)
                                        ctx.lineTo(14.5, 3.5)
                                        ctx.moveTo(13.5, 2.2)
                                        ctx.lineTo(13.5, 4.8)
                                        ctx.stroke()
                                    }
                                }

                                MouseArea {
                                    id: watchLaterEntryArea
                                    anchors.fill: parent
                                    onClicked: {
                                        favView = 6
                                        if (controller) Qt.callLater(function() { controller.fetchWatchLater(1, 20) })
                                    }
                                }
                            }

                            Text {
                                text: "稍后再看"
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
                                color: settingsEntryArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.withAlpha(Theme.primary, 0.12)
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
                                        ctx.lineWidth = 1.6
                                        ctx.lineCap = "round"
                                        ctx.beginPath()
                                        ctx.arc(8, 8, 2.2, 0, Math.PI * 2)
                                        ctx.stroke()
                                        for (var i = 0; i < 8; i++) {
                                            var a = i * Math.PI / 4
                                            var x1 = 8 + Math.cos(a) * 4.2
                                            var y1 = 8 + Math.sin(a) * 4.2
                                            var x2 = 8 + Math.cos(a) * 6.8
                                            var y2 = 8 + Math.sin(a) * 6.8
                                            ctx.beginPath()
                                            ctx.moveTo(x1, y1)
                                            ctx.lineTo(x2, y2)
                                            ctx.stroke()
                                        }
                                    }
                                }

                                MouseArea {
                                    id: settingsEntryArea
                                    anchors.fill: parent
                                    onClicked: favView = 4
                                }
                            }

                            Text {
                                text: "设置"
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
                onCancelRequested: {
                    if (controller) controller.cancelAll();
                }
            }
        }

        // ── 最近观看 ──
        Item {
            id: recentHistoryView
            anchors.fill: parent
            visible: favView === 3

            // 用 Loader 延迟创建 ListView，避免 UserPage 被加载时就构建大量 delegate 导致卡顿
            Loader {
                id: recentLoader
                anchors.fill: parent
                active: recentHistoryView.visible
                asynchronous: true
                sourceComponent: Component {
                    Item {
                        anchors.fill: parent

                        function restorePosition() {
                            if (recentHistoryView.visible && userPage.recentHistoryContentX > 0) {
                                recentList.contentX = userPage.recentHistoryContentX
                            }
                        }

                        ListView {
                            id: recentList
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSmall
                            model: controller ? controller.recentHistoryModel() : null
                            orientation: ListView.Horizontal
                            spacing: Theme.spacingMedium
                            clip: true

                            // 性能参数对齐 HomePage
                            cacheBuffer: 640
                            displayMarginBeginning: 160
                            displayMarginEnd: 160

                            property bool _loadingMore: false
                            onAtXEndChanged: {
                                if (!atXEnd || !controller || _loadingMore) return
                                _loadingMore = true
                                Qt.callLater(function() {
                                    controller.fetchMoreRecentHistory()
                                    _loadingMore = false
                                })
                            }

                            delegate: Components.VideoCardCompact {
                                height: recentList.height
                                videoTitle: model.title || ""
                                // 与 HomePage 保持一致：直接使用 model.pic，避免重复 encode 带来额外开销/错误
                                coverUrl: model.pic || ""
                                upName: model.ownerName || ""
                                viewCount: ""
                                durationText: model.durationText || ""
                                bvid: model.bvid || ""
                                onClicked: {
                                    userPage.recentHistoryContentX = recentList.contentX
                                    userPage.videoSelected(bvid)
                                }
                            }
                        }

                        Text {
                            visible: recentList.count === 0
                            text: "暂无最近观看"
                            color: Theme.textTertiary
                            anchors.centerIn: parent
                        }

                        Component.onCompleted: {
                            Qt.callLater(function() { restorePosition() })
                        }
                    }
                }
            }
        }

        // ── 稍后再看 ──
        Item {
            id: watchLaterView
            anchors.fill: parent
            visible: favView === 6

            Loader {
                id: watchLaterLoader
                anchors.fill: parent
                active: watchLaterView.visible
                asynchronous: true
                sourceComponent: Component {
                    Item {
                        anchors.fill: parent

                        function restorePosition() {
                            if (watchLaterView.visible && userPage.watchLaterContentX > 0) {
                                watchLaterList.contentX = userPage.watchLaterContentX
                            }
                        }

                        ListView {
                            id: watchLaterList
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSmall
                            model: controller ? controller.watchLaterModel() : null
                            orientation: ListView.Horizontal
                            spacing: Theme.spacingMedium
                            clip: true

                            cacheBuffer: 640
                            displayMarginBeginning: 160
                            displayMarginEnd: 160

                            property bool _loadingMore: false
                            onAtXEndChanged: {
                                if (!atXEnd || !controller || _loadingMore) return
                                _loadingMore = true
                                Qt.callLater(function() {
                                    controller.fetchMoreWatchLater()
                                    _loadingMore = false
                                })
                            }

                            delegate: Components.VideoCardCompact {
                                height: watchLaterList.height
                                videoTitle: model.title || ""
                                coverUrl: model.pic || ""
                                upName: model.ownerName || ""
                                viewCount: ""
                                durationText: model.durationText || ""
                                bvid: model.bvid || ""
                                onClicked: {
                                    userPage.watchLaterContentX = watchLaterList.contentX
                                    userPage.videoSelected(bvid)
                                }
                            }
                        }

                        Text {
                            visible: watchLaterList.count === 0 && controller && !controller.isLoading
                            text: "暂无稍后再看"
                            color: Theme.textTertiary
                            anchors.centerIn: parent
                        }

                        Components.LoadingIndicator {
                            anchors.centerIn: parent
                            running: controller ? controller.isLoading : false
                            onCancelRequested: {
                                if (controller) controller.cancelAll();
                            }
                        }

                        Component.onCompleted: {
                            Qt.callLater(function() { restorePosition() })
                        }
                    }
                }
            }
        }

        // ── 设置页 ──
        SettingsPage {
            id: settingsPage
            anchors.fill: parent
            visible: favView === 4 || favView === 5
            controller: userPage.controller
            viewMode: favView
            onRequestSubtitleSettings: userPage.favView = 5
        }

        // ── 收藏夹详情 ──
        Item {
            id: favDetailView
            anchors.fill: parent
            visible: favView === 2

            Loader {
                id: favDetailLoader
                anchors.fill: parent
                active: favDetailView.visible
                asynchronous: true
                sourceComponent: Component {
                    Item {
                        anchors.fill: parent

                        ListView {
                            id: favItems
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSmall
                            model: controller ? controller.favoriteItemModel() : null
                            orientation: ListView.Horizontal
                            spacing: Theme.spacingMedium
                            clip: true

                            cacheBuffer: 640
                            displayMarginBeginning: 160
                            displayMarginEnd: 160

                            delegate: Components.VideoCardCompact {
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

                            property bool _loadingMore: false
                            onAtXEndChanged: {
                                if (!atXEnd || !controller || _loadingMore) return
                                _loadingMore = true
                                Qt.callLater(function() {
                                    controller.fetchMoreFavoriteItems()
                                    _loadingMore = false
                                })
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
                            onCancelRequested: {
                                if (controller) controller.cancelAll();
                            }
                        }
                    }
                }
            }
        }
    }

}
