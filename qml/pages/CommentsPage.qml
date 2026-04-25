import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Rectangle {
    id: commentsPage
    width: 320
    height: 170
    color: Theme.bgPrimary

    // 字体清晰度：在小字号+深色背景下，NativeRendering+强 Hinting 容易出现横竖笔画粗细不一致。
    // 这里统一改为 QtRendering 并关闭 Hinting，让抗锯齿更均匀。
    readonly property int _textRenderType: Text.QtRendering
    readonly property int _hinting: Font.PreferNoHinting
    readonly property bool _textAA: true

    property var controller: null
    property int viewMode: 0 // 0=主评论列表,1=子评论详情
    property real mainCommentContentY: 0
    property var selectedComment: null
    property bool imageFullscreenVisible: false
    property string fullscreenImageUrl: ""
    signal backClicked()

    function openCommentDetail(commentObj) {
        selectedComment = commentObj
        mainCommentContentY = commentList.contentY
        viewMode = 1
        if (controller) controller.fetchCommentReplies(commentObj.rpid)
    }

    function commentImageSource(url) {
        if (!url) return ""
        if (url.indexOf("data:image/") === 0) return url
        return "image://bili/" + encodeURIComponent(url)
    }

    function firstPicture(pictures) {
        if (!pictures || pictures.length === 0) return ""
        return pictures[0] || ""
    }

    function openCommentImage(url) {
        if (!url) return
        // 系统 FileManagerImageViewer 只能打开本地文件；让 C++ 先下载到 /tmp 后发回本地路径。
        if (controller && typeof imageViewer !== "undefined" && imageViewer) {
            controller.prepareImageForViewer(url)
            return
        }
        // 兜底：宿主未注入 imageViewer 时使用旧预览。
        commentsPage.fullscreenImageUrl = url
        commentsPage.imageFullscreenVisible = !!commentsPage.fullscreenImageUrl
    }

    function openSystemImageViewer(localPath) {
        if (!localPath) return
        if (typeof imageViewer !== "undefined" && imageViewer) {
            imageViewer.open(localPath)
            id_pop_container.show("qrc:/qml/audiopages/FileManagerImageViewer.qml")
        } else {
            commentsPage.fullscreenImageUrl = localPath
            commentsPage.imageFullscreenVisible = true
        }
    }

    function internalBack() {
        if (imageFullscreenVisible) {
            imageFullscreenVisible = false
            fullscreenImageUrl = ""
            return
        }
        if (viewMode === 1) {
            viewMode = 0
            Qt.callLater(function() {
                commentList.contentY = mainCommentContentY
            })
            return
        }
        commentsPage.backClicked()
    }

    function isAnyCommentLoading() {
        if (!controller) return false
        var cm = controller.commentModel()
        var rm = controller.commentReplyModel()
        return (cm && cm.loading) || (rm && rm.loading)
    }

    Components.TitleBar {
        id: titleBar
        title: {
            if (viewMode === 1) return "评论详情";
            var cm = controller ? controller.commentModel() : null;
            var total = cm ? cm.totalCount : 0;
            return "评论" + (total > 0 ? " (" + total + ")" : "");
        }
        showBack: true
        anchors.top: parent.top
        onBackClicked: commentsPage.internalBack()
    }

    ListView {
        id: commentList
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingSmall
        model: controller ? controller.commentModel() : null
        spacing: Theme.spacingSmall
        clip: true
        visible: viewMode === 0

        delegate: Rectangle {
            width: commentList.width
            height: commentContent.height + Theme.spacingMedium * 2
            color: Theme.bgCard
            radius: Theme.radiusLarge

            Row {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingMedium

                // 头像
                Rectangle {
                    width: 22; height: 22
                    radius: Theme.radiusRound
                    color: Theme.bgTertiary
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: model.avatar
                        ? "image://bili/" + encodeURIComponent(model.avatar) : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }

                Column {
                    id: commentContent
                    width: parent.width - 22 - Theme.spacingMedium
                    spacing: Theme.spacingTiny

                    // 用户名 + 等级 + 时间
                    Row {
                        spacing: Theme.spacingSmall

                        Text {
                            text: model.userName || ""
                            color: model.isVip ? Theme.accent : Theme.primary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                            renderType: commentsPage._textRenderType
                            font.hintingPreference: commentsPage._hinting
                            antialiasing: commentsPage._textAA
                        }

                        // UP 主标识
                        Rectangle {
                            visible: controller && model.mid && controller.videoOwnerMid > 0 && (Number(model.mid) === Number(controller.videoOwnerMid))
                            height: 12
                            radius: 6
                            color: "#fb7299"
                            anchors.verticalCenter: parent.verticalCenter
                            width: upTagText.implicitWidth + 8

                            Text {
                                id: upTagText
                                anchors.centerIn: parent
                                text: "UP"
                                color: "white"
                                font.family: Theme.fontFamily
                                font.pixelSize: 8
                                font.bold: true
                                renderType: commentsPage._textRenderType
                                font.hintingPreference: commentsPage._hinting
                                antialiasing: commentsPage._textAA
                            }
                        }

                        Rectangle {
                            visible: (model.level || 0) > 0
                            width: 18; height: 10
                            radius: Theme.radiusTiny
                            anchors.verticalCenter: parent.verticalCenter
                            color: {
                                var lv = model.level || 0;
                                if (lv >= 6) return Theme.accent;
                                if (lv >= 4) return Theme.warning;
                                return Theme.textTertiary;
                            }
                            Text {
                            anchors.centerIn: parent
                            text: "L" + (model.level || 0)
                            color: "white"
                            font.pixelSize: 6
                            font.bold: true
                                renderType: commentsPage._textRenderType
                            font.hintingPreference: commentsPage._hinting
                            antialiasing: commentsPage._textAA
                        }
                        }

                        Text {
                            text: model.ctimeText || ""
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTiny
                            renderType: commentsPage._textRenderType
                            font.hintingPreference: commentsPage._hinting
                            antialiasing: commentsPage._textAA
                        }

                        Rectangle {
                            visible: !!model.isTop
                            height: 12
                            radius: 6
                            color: Theme.withAlpha(Theme.primary, 0.2)
                            border.color: Theme.withAlpha(Theme.primary, 0.45)
                            border.width: 1
                            anchors.verticalCenter: parent.verticalCenter
                            width: topLabel.implicitWidth + 8

                            Text {
                                id: topLabel
                                anchors.centerIn: parent
                                text: "TOP"
                                color: Theme.primary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTiny
                                font.bold: true
                                renderType: commentsPage._textRenderType
                                font.hintingPreference: commentsPage._hinting
                                antialiasing: commentsPage._textAA
                            }
                        }
                    }

                    // 评论内容
                    Text {
                        width: parent.width
                        text: model.content || ""
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.Wrap
                        maximumLineCount: 4
                        elide: Text.ElideRight
                        lineHeight: 1.3
                        renderType: commentsPage._textRenderType
                        font.hintingPreference: commentsPage._hinting
                        antialiasing: commentsPage._textAA
                    }

                    Rectangle {
                        visible: !!commentsPage.firstPicture(model.pictures)
                        width: Math.min(parent.width, 96)
                        height: Math.min(56, width * 0.66)
                        radius: Theme.radiusSmall
                        color: Theme.bgTertiary
                        border.color: Theme.borderLight
                        border.width: 1
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: commentsPage.commentImageSource(commentsPage.firstPicture(model.pictures))
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            mipmap: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: commentsPage.openCommentImage(commentsPage.firstPicture(model.pictures))
                        }
                    }

                    // 互动按钮
                    Row {
                        spacing: Theme.spacingLarge

                        Components.IconButton {
                            icon: "👍"
                            value: {
                                var l = model.likes || 0;
                                return l > 0
                                ? (l > 999 ? (l/1000.0).toFixed(1) + "k" : l.toString())
                                : "";
                            }
                            width: 36
                        }

                        Components.IconButton {
                            icon: "💬"
                            value: {
                                var r = model.rcount || 0;
                                return r > 0 ? r.toString() : "";
                            }
                            width: 30
                            onClicked: {
                                commentsPage.openCommentDetail({
                                    rpid: model.rpid || 0,
                                    userName: model.userName || "",
                                    avatar: model.avatar || "",
                                    level: model.level || 0,
                                    content: model.content || "",
                                    pictures: model.pictures || [],
                                    likes: model.likes || 0,
                                    ctimeText: model.ctimeText || "",
                                    isVip: model.isVip || false
                                })
                            }
                        }
                    }
                }
            }
        }

        // 加载更多
        footer: Rectangle {
            width: commentList.width
            height: Theme.touchMinSize
            color: "transparent"

            Components.IconButton {
                anchors.centerIn: parent
                width: 96
                icon: commentLoadBtnBusy ? "⏳" : "↓"
                label: commentLoadBtnBusy ? "加载中..." : "加载更多"
                active: commentLoadBtnBusy
                property bool commentLoadBtnBusy: {
                    var cm = controller ? controller.commentModel() : null
                    return cm && cm.loading
                }
                onClicked: {
                    if (!commentLoadBtnBusy && controller) controller.fetchMoreComments();
                }
            }
        }

        // 空状态
        Column {
            visible: commentList.count === 0 && controller && !commentsPage.isAnyCommentLoading()
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Text {
                text: "💬"
                font.pixelSize: 18
                anchors.horizontalCenter: parent.horizontalCenter
                opacity: 0.4
            }
            Text {
                text: "暂无评论"
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    Item {
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: viewMode === 1

        Flickable {
            id: replyDetailFlick
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            contentHeight: replyDetailColumn.height + 8
            clip: true
            boundsBehavior: Flickable.DragOverBounds

            Column {
                id: replyDetailColumn
                width: parent.width
                spacing: Theme.spacingSmall

                Rectangle {
                    width: parent.width
                    radius: Theme.radiusLarge
                    color: Theme.bgCard
                    height: mainCommentContent.height + Theme.spacingMedium * 2

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMedium
                        spacing: Theme.spacingMedium

                        Rectangle {
                            width: 22; height: 22
                            radius: Theme.radiusRound
                            color: Theme.bgTertiary
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: selectedComment && selectedComment.avatar
                                ? "image://bili/" + encodeURIComponent(selectedComment.avatar) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }

                        Column {
                            id: mainCommentContent
                            width: parent.width - 22 - Theme.spacingMedium
                            spacing: Theme.spacingTiny

                            Row {
                                spacing: Theme.spacingSmall

                                Text {
                                    text: selectedComment ? selectedComment.userName : ""
                                    color: selectedComment && selectedComment.isVip ? Theme.accent : Theme.primary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    font.bold: true
                                    renderType: commentsPage._textRenderType
                                    font.hintingPreference: commentsPage._hinting
                                    antialiasing: commentsPage._textAA
                                }

                                Text {
                                    text: selectedComment ? selectedComment.ctimeText : ""
                                    color: Theme.textTertiary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontTiny
                                    renderType: commentsPage._textRenderType
                                    font.hintingPreference: commentsPage._hinting
                                    antialiasing: commentsPage._textAA
                                }
                            }

                            Text {
                                width: parent.width
                                text: selectedComment ? selectedComment.content : ""
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontBody
                                wrapMode: Text.Wrap
                                lineHeight: 1.3
                                renderType: commentsPage._textRenderType
                                font.hintingPreference: commentsPage._hinting
                                antialiasing: commentsPage._textAA
                            }

                            Rectangle {
                                visible: selectedComment && !!commentsPage.firstPicture(selectedComment.pictures)
                                width: Math.min(parent.width, 96)
                                height: Math.min(56, width * 0.66)
                                radius: Theme.radiusSmall
                                color: Theme.bgTertiary
                                border.color: Theme.borderLight
                                border.width: 1
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: commentsPage.commentImageSource(commentsPage.firstPicture(selectedComment.pictures))
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    mipmap: true
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: commentsPage.openCommentImage(commentsPage.firstPicture(selectedComment.pictures))
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.divider
                    opacity: 0.6
                }

                Repeater {
                    model: controller ? controller.commentReplyModel() : null

                    Rectangle {
                        width: replyDetailColumn.width
                        height: replyContent.height + Theme.spacingMedium * 2
                        color: Theme.bgCard
                        radius: Theme.radiusLarge

                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingMedium
                            spacing: Theme.spacingMedium

                            Rectangle {
                                width: 22; height: 22
                                radius: Theme.radiusRound
                                color: Theme.bgTertiary
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: model.avatar
                                    ? "image://bili/" + encodeURIComponent(model.avatar) : ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                            }

                            Column {
                                id: replyContent
                                width: parent.width - 22 - Theme.spacingMedium
                                spacing: Theme.spacingTiny

                                Row {
                                    spacing: Theme.spacingSmall

                                    Text {
                                        text: model.userName || ""
                                        color: model.isVip ? Theme.accent : Theme.primary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSmall
                                        font.bold: true
                                        renderType: commentsPage._textRenderType
                                        font.hintingPreference: commentsPage._hinting
                                        antialiasing: commentsPage._textAA
                                    }

                                    Rectangle {
                                        visible: controller && model.mid && controller.videoOwnerMid > 0 && (Number(model.mid) === Number(controller.videoOwnerMid))
                                        height: 12
                                        radius: 6
                                        color: "#fb7299"
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: upReplyTagText.implicitWidth + 8

                                        Text {
                                            id: upReplyTagText
                                            anchors.centerIn: parent
                                            text: "UP"
                                            color: "white"
                                            font.family: Theme.fontFamily
                                            font.pixelSize: 8
                                            font.bold: true
                                            renderType: commentsPage._textRenderType
                                            font.hintingPreference: commentsPage._hinting
                                            antialiasing: commentsPage._textAA
                                        }
                                    }

                                    Text {
                                        text: model.ctimeText || ""
                                        color: Theme.textTertiary
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontTiny
                                        renderType: commentsPage._textRenderType
                                        font.hintingPreference: commentsPage._hinting
                                        antialiasing: commentsPage._textAA
                                    }
                                }

                                Text {
                                    width: parent.width
                                    text: model.content || ""
                                    color: Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    wrapMode: Text.Wrap
                                    lineHeight: 1.3
                                    renderType: commentsPage._textRenderType
                                    font.hintingPreference: commentsPage._hinting
                                    antialiasing: commentsPage._textAA
                                }

                                Rectangle {
                                    visible: !!commentsPage.firstPicture(model.pictures)
                                    width: Math.min(parent.width, 96)
                                    height: Math.min(56, width * 0.66)
                                    radius: Theme.radiusSmall
                                    color: Theme.bgTertiary
                                    border.color: Theme.borderLight
                                    border.width: 1
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        source: commentsPage.commentImageSource(commentsPage.firstPicture(model.pictures))
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        mipmap: true
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: commentsPage.openCommentImage(commentsPage.firstPicture(model.pictures))
                                    }
                                }
                            }
                        }
                    }
                }

                Components.IconButton {
                    visible: controller && controller.replyHasMore
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 96
                    icon: replyLoadBtnBusy ? "⏳" : "↓"
                    label: replyLoadBtnBusy ? "加载中..." : "加载更多"
                    active: replyLoadBtnBusy
                    property bool replyLoadBtnBusy: controller && controller.commentReplyModel() && controller.commentReplyModel().loading
                    onClicked: {
                        if (!replyLoadBtnBusy && controller) controller.fetchMoreCommentReplies()
                    }
                }

                Text {
                    visible: controller && controller.commentReplyModel() && controller.commentReplyModel().count === 0 && !commentsPage.isAnyCommentLoading()
                    text: "暂无回复"
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }
    }

    // ── 图片全屏查看：仅全屏预览（移除缩放/拖动/手势） ──
    Rectangle {
        id: fullscreenOverlay
        anchors.fill: parent
        visible: commentsPage.imageFullscreenVisible
        z: 1000
        color: "#E6000000"

        function close() {
            commentsPage.imageFullscreenVisible = false
            commentsPage.fullscreenImageUrl = ""
        }

        // 进入时不做任何缩放状态恢复（因为已移除缩放）

        // 背景拦截（不点击关闭，避免误触；需要可改成点击背景关闭）
        MouseArea {
            anchors.fill: parent
            z: 1
            onClicked: {
                // noop
            }
        }

        // 全屏图片：自适应显示
        Image {
            id: fullImage
            anchors.fill: parent
            anchors.margins: 0
            z: 2
            source: commentsPage.commentImageSource(commentsPage.fullscreenImageUrl)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            mipmap: true
            smooth: true
        }

        // 顶部仅保留关闭按钮
        Rectangle {
            id: topBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 34
            color: "transparent"
            z: 20

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    width: 38
                    height: 24
                    radius: 10
                    color: closeArea.pressed
                           ? Theme.withAlpha(Theme.primary, 0.22)
                           : Theme.withAlpha(Theme.bgTertiary, 0.55)
                    border.color: Theme.withAlpha(Theme.primary, 0.25)
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontNormal
                        renderType: commentsPage._textRenderType
                        font.hintingPreference: commentsPage._hinting
                        antialiasing: commentsPage._textAA
                    }

                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        onClicked: fullscreenOverlay.close()
                    }
                }
            }
        }
    }

    // ── 加载中（同 HomePage：可取消） ──
    Rectangle {
        visible: commentsPage.isAnyCommentLoading() && ((viewMode === 0 && commentList.count === 0) || (viewMode === 1 && controller && controller.commentReplyModel() && controller.commentReplyModel().count === 0))
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
                            running: commentsPage.visible && commentsPage.isAnyCommentLoading()
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

    // 系统图片查看器弹出容器
    Item {
        id: id_pop_container
        anchors.fill: parent
        z: 3000
        visible: popItemObject !== null
        property var popItemObject: null
        signal closeSameItem(string popStackId)

        function updateStackInfo() {
            if (id_pop_container.children.length > 1) {
                popItemObject = id_pop_container.children[id_pop_container.children.length - 2]
            } else {
                popItemObject = null
            }
        }

        function show(componentPath) {
            function initObj(obj) {
                if (!obj) return
                Object.defineProperty(obj, 'popStackId', {
                    enumerable: false,
                    configurable: false,
                    writable: false,
                    value: componentPath
                })
                popItemObject = obj
                if (obj.backButtonClicked) {
                    obj.backButtonClicked.connect(function() {
                        closeSameItem(obj.popStackId)
                        updateStackInfo()
                        obj.destroy(1)
                    })
                }
                id_pop_container.closeSameItem.connect(function(popStackId) {
                    if (popStackId === obj.popStackId) obj.destroy(1)
                })
                if (obj.show) obj.show()
            }

            closeSameItem(componentPath)
            var comp = Qt.createComponent(componentPath)
            if (comp.status === Component.Ready) {
                var incubator = comp.incubateObject(id_pop_container)
                if (incubator.status !== Component.Ready) {
                    incubator.onStatusChanged = function(s) {
                        if (s === Component.Ready) initObj(incubator.object)
                    }
                } else {
                    initObj(incubator.object)
                }
            } else {
                console.error("Image viewer component error: " + comp.errorString())
            }
        }
    }

    Connections {
        target: controller
        ignoreUnknownSignals: true
        function onCommentImageReadyForViewer(localPath) {
            commentsPage.openSystemImageViewer(localPath)
        }
    }

    Component.onCompleted: {
        if (controller) controller.fetchComments();
    }
}
