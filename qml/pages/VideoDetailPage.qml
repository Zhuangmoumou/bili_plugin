import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Rectangle {
    id: detailPage
    width: 320
    height: 170
    color: "#0d1117"

    property var controller: null
    property string bvid: ""
    property bool fullTitleVisible: false
    property var rootRef: null

    // 清晰度选择（默认16）
    property int selectedQuality: 16
    property var availableQualities: [16, 32, 64]

    function qualityLabel(qn) {
        switch (qn) {
        case 16: return "360P";
        case 32: return "480P";
        case 64: return "720P";
        case 80: return "1080P";
        case 112: return "1080P+";
        case 116: return "1080P60";
        case 120: return "4K";
        case 125: return "HDR";
        default: return qn + "P";
        }
    }

    function updateQualities() {
        if (controller && controller.acceptQualities && controller.acceptQualities.length > 0) {
            availableQualities = controller.acceptQualities;
        } else {
            availableQualities = [16, 32, 64];
        }
        if (availableQualities.indexOf(selectedQuality) < 0) {
            selectedQuality = availableQualities[0];
        }
        if (rootRef) {
            rootRef.playQualitySelected = selectedQuality;
        }
    }

    signal backClicked()
    signal playRequested(int quality)
    signal commentsRequested()

    readonly property string fontFamily: "Microsoft YaHei"
    readonly property color primaryColor: "#3b82f6"
    readonly property color primaryLight: "#60a5fa"
    readonly property color primaryDark: "#2563eb"

    // ═══════════════════════════════════════════════════════════
    // 全屏标题浮层
    // ═══════════════════════════════════════════════════════════
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.8)
        z: 200 // 确保在最顶层
        visible: detailPage.fullTitleVisible

        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 150 } }

        Text {
            anchors.centerIn: parent
            width: parent.width - 40
            text: controller ? controller.videoTitle : ""
            color: "white"
            font.family: fontFamily
            font.pixelSize: 14
            font.bold: true
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        MouseArea {
            anchors.fill: parent
            onClicked: detailPage.fullTitleVisible = false
        }
    }

    // 背景渐变
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0d1117" }
            GradientStop { position: 0.5; color: "#111827" }
            GradientStop { position: 1.0; color: "#0f172a" }
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        // ═══════════════════════════════════════════════════════════
        // 左侧固定列 - 装饰线
        // ═══════════════════════════════════════════════════════════
        Rectangle {
            id: leftBar
            width: 8
            height: parent.height
            color: "transparent"

            // 装饰线
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: 8
                width: 1
                color: Qt.rgba(1, 1, 1, 0.06)
            }
        }

        // ═══════════════════════════════════════════════════════════
        // 右侧主内容区 - 可垂直滑动
        // ═══════════════════════════════════════════════════════════
        Flickable {
            id: mainFlick
            width: parent.width - leftBar.width
            height: parent.height
            contentHeight: mainColumn.height + 16
            flickableDirection: Flickable.VerticalFlick
            clip: true
            boundsBehavior: Flickable.DragOverBounds

            Column {
                id: mainColumn
                width: parent.width
                spacing: 8
                topPadding: 8

                // 退出按钮
                Rectangle {
                    id: backBtn
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    width: 28
                    height: 28
                    radius: 14
                    color: backArea.pressed ? Qt.rgba(0.23, 0.51, 0.96, 0.4) : Qt.rgba(1, 1, 1, 0.1)

                    Behavior on color { ColorAnimation { duration: 150 } }
                    scale: backArea.pressed ? 0.88 : 1.0
                    Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

                    // iOS 风格返回箭头
                    Canvas {
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: -1
                        width: 14
                        height: 14
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            ctx.strokeStyle = primaryLight
                            ctx.lineWidth = 2.2
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            ctx.beginPath()
                            ctx.moveTo(9, 2)
                            ctx.lineTo(3, 7)
                            ctx.lineTo(9, 12)
                            ctx.stroke()
                        }
                    }

                    MouseArea {
                        id: backArea
                        anchors.fill: parent
                        anchors.margins: -6
                        onClicked: detailPage.backClicked()
                    }
                }

                // ─────────────────────────────────────
                // 顶部区域：封面 + 信息
                // ─────────────────────────────────────
                Item {
                    width: parent.width - 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 72

                    // 封面（圆角裁切）
                    Rectangle {
                        id: coverContainer
                        width: 108
                        height: parent.height
                        radius: 10
                        color: "#1e293b"
                        anchors.left: parent.left
                        clip: true

                        Image {
                            id: coverImage
                            anchors.fill: parent
                            source: controller && controller.videoPic
                            ? "image://bili/" + encodeURIComponent(controller.videoPic) : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                        }

                        // 渐变遮罩
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "transparent" }
                                GradientStop { position: 0.6; color: "transparent" }
                                GradientStop { position: 1.0; color: "#aa000000" }
                            }
                        }
                    }

                    // 播放按钮（在封面上层）
                    Rectangle {
                        anchors.centerIn: coverContainer
                        width: 38
                        height: 38
                        radius: 19
                        color: playArea.pressed ? primaryDark : primaryColor
                        border.color: Qt.rgba(1, 1, 1, 0.25)
                        border.width: 2

                        scale: playArea.pressed ? 0.85 : 1.0
                        Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
                        Behavior on color { ColorAnimation { duration: 120 } }

                        // 发光效果
                        Rectangle {
                            anchors.centerIn: parent
                            width: parent.width + 8
                            height: parent.height + 8
                            radius: width / 2
                            color: "transparent"
                            border.color: Qt.rgba(0.23, 0.51, 0.96, 0.35)
                            border.width: 3
                            opacity: playArea.pressed ? 0 : 0.7
                            Behavior on opacity { NumberAnimation { duration: 200 } }
                        }

                        // 播放三角形
                        Canvas {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 2
                            width: 16
                            height: 16
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.fillStyle = "white"
                                ctx.beginPath()
                                ctx.moveTo(3, 2)
                                ctx.lineTo(14, 8)
                                ctx.lineTo(3, 14)
                                ctx.closePath()
                                ctx.fill()
                            }
                        }

                        MouseArea {
                        id: playArea
                        anchors.fill: parent
                        anchors.margins: -8
                        onClicked: detailPage.playRequested(detailPage.selectedQuality)
                        }
                    }

                    // 时长标签
                    Rectangle {
                        anchors.right: coverContainer.right
                        anchors.bottom: coverContainer.bottom
                        anchors.margins: 5
                        height: 16
                        width: durationText.implicitWidth + 8
                        radius: 4
                        color: "#cc000000"

                        Text {
                            id: durationText
                            anchors.centerIn: parent
                            text: controller ? controller.videoDuration : "00:00"
                            color: "white"
                            font.pixelSize: 9
                            font.family: fontFamily
                        }
                    }

                    // 合集标识（多P视频）
                    Rectangle {
                        anchors.right: coverContainer.right
                        anchors.bottom: coverContainer.bottom
                        anchors.rightMargin: 5
                        anchors.bottomMargin: 24
                        height: 16
                        width: collectionText.implicitWidth + 10
                        radius: 4
                        color: Qt.rgba(0.6, 0.6, 0.6, 0.9)
                        visible: controller && controller.videoPartModel() && controller.videoPartModel().count > 1

                        Text {
                            id: collectionText
                            anchors.centerIn: parent
                            text: "合集"
                            color: "#222222"
                            font.pixelSize: 9
                            font.family: fontFamily
                            font.bold: true
                        }
                    }

                    // 右侧信息
                    Column {
                        anchors.left: coverContainer.right
                        anchors.leftMargin: 10
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width - 118
                        spacing: 5

                        // 标题
                        Text {
                            id: titleText
                            width: parent.width
                            height: 34
                            text: controller ? controller.videoTitle : ""
                            color: "#f1f5f9"
                            font.family: fontFamily
                            font.pixelSize: 12
                            font.bold: true
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            lineHeight: 1.2

                            opacity: controller ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 300 } }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: detailPage.fullTitleVisible = true
                            }
                        }

                        // UP 主和评论按钮
                        Flickable {
                            id: upFlick
                            width: parent.width
                            height: 20
                            contentWidth: contentRow.implicitWidth
                            flickableDirection: Flickable.HorizontalFlick
                            clip: true
                            boundsBehavior: Flickable.DragOverBounds
                            interactive: contentWidth > width

                            Row {
                                id: contentRow
                                height: parent.height
                                spacing: 8

                                // UP 主标签
                                Rectangle {
                                    height: 20
                                    radius: 10
                                    color: Qt.rgba(0.23, 0.51, 0.96, 0.15)
                                    border.color: Qt.rgba(0.23, 0.51, 0.96, 0.25)
                                    border.width: 1
                                    width: upRow.implicitWidth + 12

                                    Row {
                                        id: upRow
                                        anchors.centerIn: parent
                                        spacing: 6

                                        Rectangle {
                                            width: 16
                                            height: 16
                                            radius: 8
                                            color: "#1e293b"
                                            clip: true
                                            anchors.verticalCenter: parent.verticalCenter

                                            Image {
                                                anchors.fill: parent
                                                source: controller && controller.videoOwnerFace
                                                ? "image://bili/" + encodeURIComponent(controller.videoOwnerFace) : ""
                                                fillMode: Image.PreserveAspectCrop
                                                asynchronous: true
                                            }
                                        }

                                        Text {
                                            text: controller ? controller.videoOwner : ""
                                            color: primaryLight
                                            font.family: fontFamily
                                            font.pixelSize: 10
                                            font.bold: true
                                            anchors.verticalCenter: parent.verticalCenter
                                            elide: Text.ElideRight
                                            width: Math.min(implicitWidth, 100)
                                        }
                                    }
                                }

                                // 评论按钮
                                Rectangle {
                                    width: 72
                                    height: 20
                                    radius: 10
                                    color: commentArea.pressed ? primaryDark : primaryColor

                                    scale: commentArea.pressed ? 0.92 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                                    Behavior on color { ColorAnimation { duration: 100 } }

                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 4

                                        // 评论图标
                                        Canvas {
                                            width: 11
                                            height: 11
                                            anchors.verticalCenter: parent.verticalCenter
                                            onPaint: {
                                                var ctx = getContext("2d")
                                                ctx.clearRect(0, 0, width, height)
                                                ctx.strokeStyle = "white"
                                                ctx.lineWidth = 1.2
                                                ctx.lineCap = "round"
                                                ctx.lineJoin = "round"
                                                ctx.beginPath()
                                                ctx.moveTo(2, 1)
                                                ctx.lineTo(9, 1)
                                                ctx.quadraticCurveTo(10, 1, 10, 2)
                                                ctx.lineTo(10, 6)
                                                ctx.quadraticCurveTo(10, 7, 9, 7)
                                                ctx.lineTo(4, 7)
                                                ctx.lineTo(2, 10)
                                                ctx.lineTo(2, 7)
                                                ctx.lineTo(2, 7)
                                                ctx.quadraticCurveTo(1, 7, 1, 6)
                                                ctx.lineTo(1, 2)
                                                ctx.quadraticCurveTo(1, 1, 2, 1)
                                                ctx.stroke()
                                            }
                                        }

                                        Text {
                                            text: "查看评论"
                                            color: "white"
                                            font.family: fontFamily
                                            font.pixelSize: 9
                                            font.bold: true
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    MouseArea {
                                        id: commentArea
                                        anchors.fill: parent
                                        onClicked: detailPage.commentsRequested()
                                    }
                                }

                                // 下载按钮
                                Rectangle {
                                    width: 48
                                    height: 20
                                    radius: 10
                                    color: downloadArea.pressed ? primaryDark : primaryColor

                                    scale: downloadArea.pressed ? 0.92 : 1.0
                                    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                                    Behavior on color { ColorAnimation { duration: 100 } }

                                    Text {
                                        text: "下载"
                                        color: "white"
                                        font.family: fontFamily
                                        font.pixelSize: 9
                                        font.bold: true
                                        anchors.centerIn: parent
                                    }

                                    MouseArea {
                                        id: downloadArea
                                        anchors.fill: parent
                                        onClicked: {
                                            if (controller) {
                                                controller.downloadVideoToDisk(detailPage.selectedQuality);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ─────────────────────────────────────
                // 清晰度选择器
                // ─────────────────────────────────────
                Row {
                    id: qualityRow
                    spacing: 6
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16

                    Text {
                        text: "清晰度: " + qualityLabel(detailPage.selectedQuality)
                        color: primaryLight
                        font.family: fontFamily
                        font.pixelSize: 10
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Repeater {
                        model: detailPage.availableQualities

                        Rectangle {
                            height: 18
                            width: Math.max(38, qualityText.implicitWidth + 10)
                            radius: 9
                            color: detailPage.selectedQuality === modelData
                                   ? primaryColor
                                   : Qt.rgba(1, 1, 1, 0.08)
                            border.width: 1
                            border.color: detailPage.selectedQuality === modelData
                                           ? primaryLight
                                           : Qt.rgba(1, 1, 1, 0.12)

                            Text {
                                id: qualityText
                                anchors.centerIn: parent
                                text: qualityLabel(modelData) + (detailPage.selectedQuality === modelData ? " ✓" : "")
                                color: detailPage.selectedQuality === modelData
                                       ? "white"
                                       : "#cbd5e1"
                                font.family: fontFamily
                                font.pixelSize: 9
                                font.bold: detailPage.selectedQuality === modelData
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    detailPage.selectedQuality = modelData
                                    if (detailPage.rootRef) {
                                        detailPage.rootRef.playQualitySelected = modelData
                                    }
                                }
                            }
                        }
                    }
                }

                // ─────────────────────────────────────
                // Badge 信息栏（可横向滑动）
                // ─────────────────────────────────────
                Flickable {
                    id: badgeFlick
                    width: parent.width
                    height: 28
                    contentWidth: badgeRow.width
                    flickableDirection: Flickable.HorizontalFlick
                    clip: true
                    boundsBehavior: Flickable.DragOverBounds

                    Row {
                        id: badgeRow
                        height: parent.height
                        spacing: 6
                        leftPadding: 8
                        rightPadding: 8

                        // 播放量
                        BadgeItem {
                            iconType: "play"
                            value: controller ? controller.videoViews : "0"
                            iconColor: primaryLight
                        }

                        // 点赞
                        BadgeItem {
                            iconType: "like"
                            value: controller ? controller.videoLikes : "0"
                            iconColor: "#f472b6"
                        }

                        // 投币
                        BadgeItem {
                            iconType: "coin"
                            value: controller ? controller.videoCoins : "0"
                            iconColor: "#fbbf24"
                        }

                        // 收藏
                        BadgeItem {
                            iconType: "star"
                            value: controller ? controller.videoFavorites : "0"
                            iconColor: "#a78bfa"
                        }

                        // 弹幕
                        BadgeItem {
                            iconType: "danmaku"
                            value: controller ? controller.videoDanmaku : "0"
                            iconColor: "#34d399"
                        }
                    }
                }

                // ─────────────────────────────────────
                // 简介区域（大圆角矩形，可滑动）
                // ─────────────────────────────────────
                Rectangle {
                    id: descCard
                    width: parent.width - 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    radius: 12
                    color: Qt.rgba(1, 1, 1, 0.05)
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                    border.width: 1
                    height: descColumn.implicitHeight + 20

                    Column {
                        id: descColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        spacing: 6

                        // 简介标题
                        Row {
                            spacing: 5
                            height: 12

                            Canvas {
                                width: 12
                                height: 12
                                anchors.bottom: parent.bottom
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    ctx.strokeStyle = primaryLight
                                    ctx.lineWidth = 1.3
                                    ctx.lineCap = "round"
                                    ctx.beginPath()
                                    ctx.moveTo(2, 1)
                                    ctx.lineTo(7, 1)
                                    ctx.lineTo(10, 4)
                                    ctx.lineTo(10, 11)
                                    ctx.lineTo(2, 11)
                                    ctx.closePath()
                                    ctx.stroke()
                                    ctx.beginPath()
                                    ctx.moveTo(7, 1)
                                    ctx.lineTo(7, 4)
                                    ctx.lineTo(10, 4)
                                    ctx.stroke()
                                    ctx.beginPath()
                                    ctx.moveTo(4, 6)
                                    ctx.lineTo(8, 6)
                                    ctx.moveTo(4, 8.5)
                                    ctx.lineTo(8, 8.5)
                                    ctx.stroke()
                                }
                            }

                            Text {
                                text: "简介"
                                color: primaryLight
                                font.family: fontFamily
                                font.pixelSize: 11
                                font.bold: true
                                anchors.bottom: parent.bottom
                            }

                            Text {
                                text: controller ? ("发布于 " + controller.videoPubDate) : ""
                                color: "#94a3b8"
                                font.family: fontFamily
                                font.pixelSize: 9
                                anchors.bottom: parent.bottom
                            }
                        }

                        // 分割线
                        Rectangle {
                            width: parent.width
                            height: 1
                            color: Qt.rgba(1, 1, 1, 0.08)
                        }

                        // 简介内容（可滑动）
                        Flickable {
                            id: descFlick
                            width: parent.width
                            height: Math.min(descText.implicitHeight, 80)
                            contentHeight: descText.implicitHeight
                            flickableDirection: Flickable.VerticalFlick
                            clip: true
                            boundsBehavior: Flickable.DragOverBounds

                            Text {
                                id: descText
                                width: parent.width
                                text: controller && controller.videoDesc
                                ? controller.videoDesc : "暂无简介"
                                color: "#94a3b8"
                                font.family: fontFamily
                                font.pixelSize: 10
                                wrapMode: Text.Wrap
                                lineHeight: 1.35
                            }
                        }
                    }
                }

                // ─────────────────────────────────────
                // 分P 列表
                // ─────────────────────────────────────
                Item {
                    width: parent.width
                    height: videoPartList.visible ? videoPartList.height + 24 : 0
                    visible: !controller.isLoading && videoPartList.model && videoPartList.model.count > 0

                    Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                    // 分P列表标题
                    Row {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.leftMargin: 16
                        spacing: 5
                        height: 12
                        visible: videoPartList.visible

                        Canvas {
                            width: 12
                            height: 12
                            anchors.bottom: parent.bottom
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = primaryLight
                                ctx.lineWidth = 1.3
                                ctx.lineCap = "round"
                                ctx.beginPath()
                                ctx.moveTo(1, 2); ctx.lineTo(1, 10);
                                ctx.moveTo(4, 2); ctx.lineTo(4, 10);
                                ctx.moveTo(7, 2); ctx.lineTo(7, 10);
                                ctx.moveTo(10, 2); ctx.lineTo(10, 10);
                                ctx.stroke()
                            }
                        }

                        Text {
                            text: "视频选集"
                            color: primaryLight
                            font.family: fontFamily
                            font.pixelSize: 11
                            font.bold: true
                            anchors.bottom: parent.bottom
                        }
                    }

                    // 水平分P列表
                    ListView {
                        id: videoPartList
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 20
                        height: 60
                        orientation: ListView.Horizontal
                        clip: true
                        spacing: 8
                        leftMargin: 16
                        rightMargin: 16
                        
                        model: controller ? controller.videoPartModel() : null
                        visible: model && model.count > 0

                        delegate: Components.VideoPartCard {
                            pNumber: model.page
                            partTitle: model.part
                            durationText: model.durationText
                            isCurrent: controller && controller.videoCid === model.cid

                            onClicked: {
                                if (controller) {
                                    controller.playVideoPart(index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // Badge 组件
    // ═══════════════════════════════════════════════════════════
    component BadgeItem: Rectangle {
        property string iconType: ""
        property string value: ""
        property color iconColor: primaryLight

        width: badgeContent.width + 14
        height: 24
        radius: 12
        color: Qt.rgba(1, 1, 1, 0.07)
        border.color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
        anchors.verticalCenter: parent.verticalCenter

        Row {
            id: badgeContent
            anchors.centerIn: parent
            spacing: 4

            Canvas {
                width: 12
                height: 12
                anchors.verticalCenter: parent.verticalCenter

                property string type: iconType
                property color col: iconColor

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    if (type === "play") {
                        ctx.fillStyle = col
                        ctx.beginPath()
                        ctx.moveTo(2, 1)
                        ctx.lineTo(11, 6)
                        ctx.lineTo(2, 11)
                        ctx.closePath()
                        ctx.fill()
                    } else if (type === "like") {
                        ctx.fillStyle = col
                        ctx.beginPath()
                        ctx.moveTo(6, 11)
                        ctx.bezierCurveTo(1, 7, 0, 4, 2.5, 2)
                        ctx.bezierCurveTo(4, 1, 6, 2, 6, 4)
                        ctx.bezierCurveTo(6, 2, 8, 1, 9.5, 2)
                        ctx.bezierCurveTo(12, 4, 11, 7, 6, 11)
                        ctx.fill()
                    } else if (type === "coin") {
                        ctx.strokeStyle = col
                        ctx.lineWidth = 1.3
                        ctx.beginPath()
                        ctx.arc(6, 6, 5, 0, Math.PI * 2)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.arc(6, 6, 2.5, 0, Math.PI * 2)
                        ctx.stroke()
                    } else if (type === "star") {
                        ctx.fillStyle = col
                        ctx.beginPath()
                        var cx = 6, cy = 6, outerR = 5.5, innerR = 2.2
                        for (var i = 0; i < 5; i++) {
                            var outerAngle = (i * 72 - 90) * Math.PI / 180
                            var innerAngle = ((i * 72) + 36 - 90) * Math.PI / 180
                            if (i === 0) {
                                ctx.moveTo(cx + outerR * Math.cos(outerAngle), cy + outerR * Math.sin(outerAngle))
                            } else {
                                ctx.lineTo(cx + outerR * Math.cos(outerAngle), cy + outerR * Math.sin(outerAngle))
                            }
                            ctx.lineTo(cx + innerR * Math.cos(innerAngle), cy + innerR * Math.sin(innerAngle))
                        }
                        ctx.closePath()
                        ctx.fill()
                    } else if (type === "danmaku") {
                        ctx.strokeStyle = col
                        ctx.lineWidth = 1.4
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.moveTo(0, 3)
                        ctx.lineTo(9, 3)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(2, 6)
                        ctx.lineTo(12, 6)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(0, 9)
                        ctx.lineTo(7, 9)
                        ctx.stroke()
                    }
                }
            }

            Text {
                text: value
                color: "#d1d5db"
                font.family: fontFamily
                font.pixelSize: 10
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 下载进度遮罩
    // ═══════════════════════════════════════════════════════════
    Rectangle {
        id: downloadProgressOverlay
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.75)
        visible: controller && controller.isDownloading
        z: 100 // 确保在最顶层

        Behavior on opacity { NumberAnimation { duration: 200 } }
        opacity: visible ? 1 : 0

        Column {
            anchors.centerIn: parent
            width: parent.width - 80
            spacing: 8

            Text {
                text: controller ? controller.downloadStatus : "准备下载..."
                color: "#FFFFFF"
                font.family: fontFamily
                font.pixelSize: 12
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.WordWrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                width: parent.width
                height: 4
                radius: 2
                color: Qt.rgba(1, 1, 1, 0.3)
                
                Rectangle {
                    width: parent.width * (controller ? controller.downloadProgress : 0)
                    height: parent.height
                    radius: 2
                    color: primaryColor
                    Behavior on width { NumberAnimation { duration: 150 } }
                }
            }
        }

        // 点击取消下载
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (controller) {
                    controller.cancelDownload();
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 加载指示器
    // ═══════════════════════════════════════════════════════════
    Rectangle {
        id: loadingOverlay
        anchors.centerIn: parent
        width: 72
        height: 72
        radius: 18
        color: "#ee0d1117"
        border.color: Qt.rgba(1, 1, 1, 0.1)
        border.width: 1
        visible: controller ? controller.isLoading : false
        opacity: visible ? 1 : 0
        scale: visible ? 1 : 0.8

        Behavior on opacity { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 250; easing.type: Easing.OutBack } }

        Column {
            anchors.centerIn: parent
            spacing: 8

            Canvas {
                id: loadingSpinner
                width: 28
                height: 28
                anchors.horizontalCenter: parent.horizontalCenter

                property real rotationAngle: 0

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.save()
                    ctx.translate(14, 14)
                    ctx.rotate(rotationAngle)

                    ctx.strokeStyle = primaryColor
                    ctx.lineWidth = 2.5
                    ctx.lineCap = "round"
                    ctx.beginPath()
                    ctx.arc(0, 0, 10, 0, Math.PI * 1.5)
                    ctx.stroke()

                    ctx.restore()
                }

                NumberAnimation on rotationAngle {
                    from: 0
                    to: Math.PI * 2
                    duration: 1000
                    loops: Animation.Infinite
                    running: controller ? controller.isLoading : false
                }

                onRotationAngleChanged: requestPaint()
            }

            Text {
                text: "加载中"
                color: "#94a3b8"
                font.family: fontFamily
                font.pixelSize: 10
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 入场动画
    // ═══════════════════════════════════════════════════════════
    Component.onCompleted: {
        // 仅当需要加载的 bvid 与控制器中当前的 bvid 不同时，才重新获取数据
        // 这可以防止从播放页返回时，状态被重置回 P1
        if (controller && bvid.length > 0 && controller.videoBvid !== bvid) {
            controller.fetchVideoDetail(bvid)
        }

        // 默认清晰度
        if (rootRef && rootRef.playQualitySelected > 0) {
            selectedQuality = rootRef.playQualitySelected
        }
        updateQualities()

        // 尝试获取可用清晰度
        if (controller && controller.videoCid > 0) {
            controller.fetchPlayUrl(selectedQuality)
        }

        enterAnimation.start()
    }

    Connections {
        target: controller
        function onAcceptQualitiesChanged() { detailPage.updateQualities(); }
        function onVideoDetailChanged() {
            if (controller && controller.videoCid > 0) {
                controller.fetchPlayUrl(detailPage.selectedQuality)
            }
        }
    }

    ParallelAnimation {
        id: enterAnimation

        NumberAnimation {
            target: mainColumn
            property: "opacity"
            from: 0
            to: 1
            duration: 350
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: leftBar
            property: "opacity"
            from: 0
            to: 1
            duration: 400
            easing.type: Easing.OutCubic
        }
    }
}
