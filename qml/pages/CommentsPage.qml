import QtQuick 2.12
import QtGraphicalEffects 1.12
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
    readonly property int _commentBodyFontSize: Theme.fontBody + 1
    readonly property color _pageTopGlow: "#18283b"
    readonly property color _pageBottomGlow: "#1a1320"
    readonly property color _panelFill: "#171b22"
    readonly property color _panelBorder: "#2a313d"
    readonly property color _cardFill: "#1b212a"
    readonly property color _cardFillStrong: "#202734"
    readonly property color _chipFill: "#222b37"
    readonly property color _mutedText: "#7f8a9a"
    readonly property int _headerHeight: 30
    readonly property int _metaStripHeight: 22

    property var controller: null
    property int viewMode: 0 // 0=主评论列表,1=子评论详情
    property real mainCommentContentY: 0
    property var selectedComment: null
    property bool imageFullscreenVisible: false
    property string fullscreenImageUrl: ""
    property bool autoLoadingComments: false
    property bool autoLoadingReplies: false
    property bool initialCommentsRequested: false
    property bool commentsModelAttached: false
    property bool commentImagesDeferred: false
    signal backClicked()

    function deferCommentImages() {
        commentImageResumeTimer.stop()
        commentImagesDeferred = true
    }

    function resumeCommentImagesSoon() {
        commentImageResumeTimer.restart()
    }

    Timer {
        id: commentImageResumeTimer
        interval: 120
        repeat: false
        onTriggered: commentsPage.commentImagesDeferred = false
    }

    function requestInitialComments() {
        if (!controller || initialCommentsRequested) return
        initialCommentsRequested = true
        controller.fetchComments()
        commentsModelAttached = true
    }

    Timer {
        id: initialCommentsTimer
        interval: 50
        repeat: false
        onTriggered: commentsPage.requestInitialComments()
    }

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

    function compactCount(value) {
        var n = Number(value || 0)
        if (n <= 0) return ""
        if (n >= 10000) return (n / 10000.0).toFixed(n >= 100000 ? 0 : 1) + "w"
        if (n >= 1000) return (n / 1000.0).toFixed(n >= 10000 ? 0 : 1) + "k"
        return n.toString()
    }

    function levelAccent(level) {
        switch (Number(level || 0)) {
        case 1: return "#60a5fa"
        case 2: return "#34d399"
        case 3: return "#f59e0b"
        case 4: return "#fb7185"
        case 5: return "#a78bfa"
        case 6: return "#f472b6"
        default: return Theme.primary
        }
    }

    function isOwner(mid) {
        return controller && mid && controller.videoOwnerMid > 0 && (Number(mid) === Number(controller.videoOwnerMid))
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

    function requestMoreCommentsIfNeeded() {
        if (!controller || viewMode !== 0) return
        var cm = controller.commentModel()
        if (!cm || cm.loading || autoLoadingComments || commentList.count <= 0) return
        if (commentList.contentHeight <= commentList.height) return
        autoLoadingComments = true
        controller.fetchMoreComments()
    }

    function requestMoreRepliesIfNeeded() {
        if (!controller || viewMode !== 1 || !controller.replyHasMore) return
        var rm = controller.commentReplyModel()
        if (!rm || rm.loading || autoLoadingReplies || rm.count <= 0) return
        if (replyDetailFlick.contentHeight <= replyDetailFlick.height) return
        autoLoadingReplies = true
        controller.fetchMoreCommentReplies()
    }

    function isAnyCommentLoading() {
        if (!controller) return false
        var cm = controller.commentModel()
        var rm = controller.commentReplyModel()
        return (cm && cm.loading) || (rm && rm.loading)
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#0b1017" }
            GradientStop { position: 0.55; color: "#0f151d" }
            GradientStop { position: 1.0; color: "#0c1118" }
        }
    }

    Rectangle {
        width: 110
        height: 60
        radius: 30
        anchors.right: parent.right
        anchors.rightMargin: -28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -18
        color: Theme.withAlpha(_pageBottomGlow, 0.55)
    }

    Item {
        id: headerWrap
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 28
        z: 20

        Rectangle {
            id: backButton
            width: 20
            height: 20
            radius: 10
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 4
            color: backButtonArea.pressed ? Theme.withAlpha(Theme.primary, 0.26) : Theme.withAlpha(_panelFill, 0.88)
            border.color: Theme.withAlpha(Theme.primary, backButtonArea.pressed ? 0.32 : 0.16)
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "‹"
                color: Theme.primary
                font.pixelSize: Theme.fontLarge
                font.bold: true
            }

            MouseArea {
                id: backButtonArea
                anchors.fill: parent
                anchors.margins: -8
                onClicked: commentsPage.internalBack()
            }
        }

        Rectangle {
            id: titleChip
            anchors.left: backButton.right
            anchors.leftMargin: 6
            anchors.top: parent.top
            anchors.topMargin: 4
            height: 20
            width: titleText.implicitWidth + 12
            radius: 10
            color: Theme.withAlpha(_panelFill, 0.9)
            border.color: Theme.withAlpha(_panelBorder, 0.82)
            border.width: 1

            Text {
                id: titleText
                anchors.centerIn: parent
                text: viewMode === 1 ? "评论详情" : "评论区"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontMedium
                font.bold: true
                renderType: commentsPage._textRenderType
                font.hintingPreference: commentsPage._hinting
                antialiasing: commentsPage._textAA
            }
        }

        Rectangle {
            id: headerCountChip
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.top: parent.top
            anchors.topMargin: 6
            width: headerCountText.implicitWidth + 12
            height: 15
            radius: 8
            color: Theme.withAlpha(Theme.primary, 0.14)
            border.color: Theme.withAlpha(Theme.primary, 0.22)
            border.width: 1

            Text {
                id: headerCountText
                anchors.centerIn: parent
                text: {
                    if (viewMode === 1) {
                        var rm = controller ? controller.commentReplyModel() : null
                        return (rm ? rm.count : 0) + " 条回复"
                    }
                    var cm = controller ? controller.commentModel() : null
                    var total = cm ? cm.totalCount : 0
                    return (total > 0 ? total : 0) + " 条评论"
                }
                color: Theme.primaryLight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                font.bold: true
                renderType: commentsPage._textRenderType
                font.hintingPreference: commentsPage._hinting
                antialiasing: commentsPage._textAA
            }
        }

        Rectangle {
            id: metaStrip
            anchors.top: backButton.bottom
            anchors.topMargin: Theme.spacingSmall
            anchors.left: titleChip.left
            anchors.right: parent.right
            anchors.rightMargin: 8
            height: 0
            radius: 11
            color: "transparent"
            border.width: 0
            visible: false
        }
    }

    ListView {
        id: commentList
        anchors.top: headerWrap.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.bottomMargin: 4
        model: controller && commentsPage.commentsModelAttached ? controller.commentModel() : null
        spacing: 5
        clip: true
        visible: viewMode === 0
        onMovementStarted: commentsPage.deferCommentImages()
        onFlickStarted: commentsPage.deferCommentImages()
        onFlickEnded: commentsPage.resumeCommentImagesSoon()
        onMovementEnded: {
            commentsPage.resumeCommentImagesSoon()
            if (contentY + height >= contentHeight - 18) {
                commentsPage.requestMoreCommentsIfNeeded()
            }
        }
        onContentYChanged: {
            if (moving && contentY + height >= contentHeight - 10) {
                commentsPage.requestMoreCommentsIfNeeded()
            }
        }

        delegate: Rectangle {
            property bool pinned: (typeof isTop !== "undefined" && !!isTop)
                                  || (typeof is_top !== "undefined" && !!is_top)
                                  || (!!model && !!model.isTop)
                                  || (!!model && !!model.is_top)
            property string avatarImageSource: ""
            property string pictureImageSource: ""

            function loadDeferredImages() {
                if (commentsPage.commentImagesDeferred) return
                if (!avatarImageSource && model.avatar) {
                    avatarImageSource = "image://bili/" + encodeURIComponent(model.avatar)
                }
                if (!pictureImageSource) {
                    var pic = commentsPage.firstPicture(model.pictures)
                    if (pic) pictureImageSource = commentsPage.commentImageSource(pic)
                }
            }

            Component.onCompleted: Qt.callLater(loadDeferredImages)

            Connections {
                target: commentsPage
                function onCommentImagesDeferredChanged() {
                    if (!commentsPage.commentImagesDeferred) Qt.callLater(loadDeferredImages)
                }
            }

            width: commentList.width
            height: commentBodyColumn.height + 12
            radius: 12
            color: pinned ? Theme.withAlpha(_cardFillStrong, 0.98) : Theme.withAlpha(_cardFill, 0.98)
            border.color: pinned ? Theme.withAlpha(Theme.primary, 0.34) : Theme.withAlpha(_panelBorder, 0.9)
            border.width: 1

            Rectangle {
                width: 3
                height: Math.max(18, parent.height * 0.5)
                radius: 2
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 15
                anchors.bottomMargin: 8
                color: model.isVip ? Theme.accent : (pinned ? Theme.primary : Theme.withAlpha(Theme.textTertiary, 0.35))
                z: 0
            }

            Row {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6

                Rectangle {
                    width: 22
                    height: 22
                    radius: 11
                    color: Theme.bgTertiary

                    Image {
                        id: commentAvatarImage
                        anchors.fill: parent
                        source: avatarImageSource
                        sourceSize: Qt.size(44, 44)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        smooth: false
                        mipmap: false
                        visible: false
                    }

                    OpacityMask {
                        anchors.fill: commentAvatarImage
                        source: commentAvatarImage
                        maskSource: Rectangle {
                            width: commentAvatarImage.width
                            height: commentAvatarImage.height
                            radius: Math.min(width, height) / 2
                            visible: false
                        }
                    }
                }

                Column {
                    id: commentBodyColumn
                    width: parent.width - 28
                    spacing: 4

                    Row {
                        width: parent.width
                        spacing: 4

                        Text {
                            width: Math.min(90, implicitWidth)
                            text: model.userName || ""
                            color: model.isVip ? Theme.accent : Theme.textPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                            elide: Text.ElideRight
                            renderType: commentsPage._textRenderType
                            font.hintingPreference: commentsPage._hinting
                            antialiasing: commentsPage._textAA
                        }

                        Rectangle {
                            visible: commentsPage.isOwner(model.mid)
                            width: ownerTagText.implicitWidth + 8
                            height: 12
                            radius: 6
                            color: Theme.withAlpha(Theme.accent, 0.18)
                            border.color: Theme.withAlpha(Theme.accent, 0.28)
                            border.width: 1

                            Text {
                                id: ownerTagText
                                anchors.centerIn: parent
                                text: "UP"
                                color: Theme.accent
                                font.family: Theme.fontFamily
                                font.pixelSize: 7
                                font.bold: true
                            }
                        }

                        Rectangle {
                            visible: (model.level || 0) > 0
                            width: levelText.implicitWidth + 8
                            height: 12
                            radius: 6
                            color: Theme.withAlpha(commentsPage.levelAccent(model.level || 0), 0.16)
                            border.color: Theme.withAlpha(commentsPage.levelAccent(model.level || 0), 0.30)
                            border.width: 1

                            Text {
                                id: levelText
                                anchors.centerIn: parent
                                text: "Lv" + (model.level || 0)
                                color: commentsPage.levelAccent(model.level || 0)
                                font.family: Theme.fontFamily
                                font.pixelSize: 7
                                font.bold: true
                            }
                        }

                        Rectangle {
                            visible: pinned
                            width: topTagText.implicitWidth + 10
                            height: 13
                            radius: 6
                            color: Qt.rgba(0.23, 0.51, 0.96, 0.18)
                            border.color: Qt.rgba(0.38, 0.70, 1.0, 0.36)
                            border.width: 1

                            Text {
                                id: topTagText
                                anchors.centerIn: parent
                                text: "TOP"
                                color: "#93c5fd"
                                font.family: Theme.fontFamily
                                font.pixelSize: 7
                                font.bold: true
                            }
                        }

                        Item {
                            width: Math.max(0, parent.width - 170)
                            height: 1
                        }

                        Text {
                            width: 50
                            horizontalAlignment: Text.AlignRight
                            text: model.ctimeText || ""
                            color: _mutedText
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
                        font.pixelSize: commentsPage._commentBodyFontSize
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        lineHeight: 1.22
                        renderType: commentsPage._textRenderType
                        font.hintingPreference: commentsPage._hinting
                        antialiasing: commentsPage._textAA
                    }

                    Row {
                        width: parent.width
                        spacing: 6

                        Rectangle {
                            visible: !!commentsPage.firstPicture(model.pictures)
                            width: 58
                            height: 36
                            radius: 8
                            color: Theme.bgTertiary
                            border.color: Theme.withAlpha(_panelBorder, 0.95)
                            border.width: 1
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: pictureImageSource
                                sourceSize: Qt.size(116, 72)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: false
                                mipmap: false
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: commentsPage.openCommentImage(commentsPage.firstPicture(model.pictures))
                            }
                        }

                        Item {
                            width: Math.max(0, parent.width - (commentsPage.firstPicture(model.pictures) ? 64 : 0) - actionButtons.width)
                            height: 1
                        }

                        Row {
                            id: actionButtons
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            Components.IconButton {
                                icon: "👍"
                                value: commentsPage.compactCount(model.likes || 0)
                                width: 30
                            }

                            Components.IconButton {
                                icon: "💬"
                                value: commentsPage.compactCount(model.rcount || 0)
                                width: 32
                                active: false
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
                                        isVip: model.isVip || false,
                                        pinned: pinned
                                    })
                                }
                            }
                        }
                    }
                }
            }
        }

        footer: Rectangle {
            width: commentList.width
            height: 30
            radius: 10
            color: Theme.withAlpha(_panelFill, 0.7)
            border.color: Theme.withAlpha(_panelBorder, 0.7)
            border.width: 1

            Components.IconButton {
                anchors.centerIn: parent
                width: 92
                icon: commentLoadBtnBusy ? "⏳" : "↓"
                label: commentLoadBtnBusy ? "加载中..." : "更多评论"
                active: commentLoadBtnBusy
                property bool commentLoadBtnBusy: {
                    var cm = controller ? controller.commentModel() : null
                    return cm && cm.loading
                }
                onClicked: {
                    if (!commentLoadBtnBusy && controller) controller.fetchMoreComments()
                }
            }
        }

        Column {
            visible: commentsPage.initialCommentsRequested && commentList.count === 0 && controller && !commentsPage.isAnyCommentLoading()
            anchors.centerIn: parent
            spacing: 2

            Text {
                text: "评论还没刷出来"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: "稍后再试"
                color: Theme.textTertiary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    Item {
        anchors.top: headerWrap.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.bottomMargin: 4
        visible: viewMode === 1

        ListView {
            id: replyDetailFlick
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.DragOverBounds
            cacheBuffer: 60
            spacing: 5
            model: controller ? controller.commentReplyModel() : null

            onMovementStarted: commentsPage.deferCommentImages()
            onMovementEnded: {
                commentsPage.resumeCommentImagesSoon()
                if (contentY + height >= contentHeight - 18) {
                    commentsPage.requestMoreRepliesIfNeeded()
                }
            }
            onContentYChanged: {
                if (moving && contentY + height >= contentHeight - 10) {
                    commentsPage.requestMoreRepliesIfNeeded()
                }
            }

            header: Column {
                width: replyDetailFlick.width
                spacing: 5

                Rectangle {
                    width: parent.width
                    height: detailHeaderColumn.height + 12
                    radius: 12
                    color: Theme.withAlpha(_cardFillStrong, 0.98)
                    border.color: Theme.withAlpha(Theme.primary, 0.24)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 6

                        Rectangle {
                            width: 24
                            height: 24
                            radius: 12
                            color: Theme.bgTertiary

                            Image {
                                id: detailAvatarImage
                                anchors.fill: parent
                                source: selectedComment && selectedComment.avatar
                                        ? "image://bili/" + encodeURIComponent(selectedComment.avatar) : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: true
                                mipmap: true
                                visible: false
                            }

                            OpacityMask {
                                anchors.fill: detailAvatarImage
                                source: detailAvatarImage
                                maskSource: Rectangle {
                                    width: detailAvatarImage.width
                                    height: detailAvatarImage.height
                                    radius: Math.min(width, height) / 2
                                    visible: false
                                }
                            }
                        }

                        Column {
                            id: detailHeaderColumn
                            width: parent.width - 30
                            spacing: 4

                            Row {
                                width: parent.width
                                spacing: 4

                                Text {
                                    id: detailUserNameText
                                    width: Math.min(88, implicitWidth)
                                    text: selectedComment ? selectedComment.userName : ""
                                    color: selectedComment && selectedComment.isVip ? Theme.accent : Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSmall
                                    font.bold: true
                                    elide: Text.ElideRight
                                    renderType: commentsPage._textRenderType
                                    font.hintingPreference: commentsPage._hinting
                                    antialiasing: commentsPage._textAA
                                }

                                Rectangle {
                                    id: detailTopTag
                                    visible: selectedComment && selectedComment.pinned
                                    width: detailTopTagText.implicitWidth + 10
                                    height: 13
                                    radius: 6
                                    color: Qt.rgba(0.23, 0.51, 0.96, 0.18)
                                    border.color: Qt.rgba(0.38, 0.70, 1.0, 0.36)
                                    border.width: 1

                                    Text {
                                        id: detailTopTagText
                                        anchors.centerIn: parent
                                        text: "TOP"
                                        color: "#93c5fd"
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 7
                                        font.bold: true
                                    }
                                }

                                Text {
                                    id: detailTimeText
                                    width: 50
                                    horizontalAlignment: Text.AlignRight
                                    text: selectedComment ? selectedComment.ctimeText : ""
                                    color: _mutedText
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontTiny
                                    renderType: commentsPage._textRenderType
                                    font.hintingPreference: commentsPage._hinting
                                    antialiasing: commentsPage._textAA
                                }

                                Item {
                                    width: Math.max(0, parent.width
                                                    - detailUserNameText.width
                                                    - (detailTopTag.visible ? detailTopTag.width : 0)
                                                    - detailTimeText.width
                                                    - detailLikeBadge.width
                                                    - (detailTopTag.visible ? 20 : 16))
                                    height: 1
                                }

                                Rectangle {
                                    id: detailLikeBadge
                                    width: detailLikeText.implicitWidth + 10
                                    height: 14
                                    radius: 7
                                    color: Theme.withAlpha(Theme.primary, 0.12)
                                    border.color: Theme.withAlpha(Theme.primary, 0.2)
                                    border.width: 1

                                    Text {
                                        id: detailLikeText
                                        anchors.centerIn: parent
                                        text: "赞 " + commentsPage.compactCount(selectedComment ? selectedComment.likes : 0)
                                        color: Theme.primaryLight
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 7
                                    }
                                }
                            }

                            Text {
                                width: parent.width
                                text: selectedComment ? selectedComment.content : ""
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: commentsPage._commentBodyFontSize
                                wrapMode: Text.Wrap
                                lineHeight: 1.24
                                renderType: commentsPage._textRenderType
                                font.hintingPreference: commentsPage._hinting
                                antialiasing: commentsPage._textAA
                            }

                            Rectangle {
                                visible: selectedComment && !!commentsPage.firstPicture(selectedComment.pictures)
                                width: 64
                                height: 40
                                radius: 8
                                color: Theme.bgTertiary
                                border.color: Theme.withAlpha(_panelBorder, 0.95)
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
                    height: 18
                    radius: 9
                    color: Theme.withAlpha(_chipFill, 0.92)
                    border.color: Theme.withAlpha(_panelBorder, 0.8)
                    border.width: 1

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        Text {
                            text: "回复列表"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTiny
                        }

                        Text {
                            text: controller && controller.commentReplyModel() ? controller.commentReplyModel().count + " 条" : "0 条"
                            color: _mutedText
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTiny
                        }
                    }
                }
            }

            delegate: Rectangle {
                width: replyDetailFlick.width
                height: replyContent.height + 12
                radius: 12
                color: Theme.withAlpha(_cardFill, 0.98)
                border.color: Theme.withAlpha(_panelBorder, 0.9)
                border.width: 1
                property string replyAvatarSource: ""
                property string replyPictureSource: ""

                function loadReplyImages() {
                    if (commentsPage.commentImagesDeferred) return
                    if (!replyAvatarSource && model.avatar) {
                        replyAvatarSource = "image://bili/" + encodeURIComponent(model.avatar)
                    }
                    if (!replyPictureSource) {
                        var pic = commentsPage.firstPicture(model.pictures)
                        if (pic) replyPictureSource = commentsPage.commentImageSource(pic)
                    }
                }

                Component.onCompleted: Qt.callLater(loadReplyImages)

                Connections {
                    target: commentsPage
                    function onCommentImagesDeferredChanged() {
                        if (!commentsPage.commentImagesDeferred) Qt.callLater(loadReplyImages)
                    }
                }

                Row {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6

                    Rectangle {
                        width: 20
                        height: 20
                        radius: 10
                        color: Theme.bgTertiary

                        Image {
                            id: replyAvatarImage
                            anchors.fill: parent
                            source: replyAvatarSource
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            smooth: true
                            mipmap: true
                            visible: false
                        }

                        OpacityMask {
                            anchors.fill: replyAvatarImage
                            source: replyAvatarImage
                            maskSource: Rectangle {
                                width: replyAvatarImage.width
                                height: replyAvatarImage.height
                                radius: Math.min(width, height) / 2
                                visible: false
                            }
                        }
                    }

                    Column {
                        id: replyContent
                        width: parent.width - 26
                        spacing: 4

                        Row {
                            width: parent.width
                            spacing: 4

                            Text {
                                width: Math.min(84, implicitWidth)
                                text: model.userName || ""
                                color: model.isVip ? Theme.accent : Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSmall
                                font.bold: true
                                elide: Text.ElideRight
                                renderType: commentsPage._textRenderType
                                font.hintingPreference: commentsPage._hinting
                                antialiasing: commentsPage._textAA
                            }

                            Rectangle {
                                visible: commentsPage.isOwner(model.mid)
                                width: replyUpTagText.implicitWidth + 8
                                height: 12
                                radius: 6
                                color: Theme.withAlpha(Theme.accent, 0.18)
                                border.color: Theme.withAlpha(Theme.accent, 0.28)
                                border.width: 1

                                Text {
                                    id: replyUpTagText
                                    anchors.centerIn: parent
                                    text: "UP"
                                    color: Theme.accent
                                    font.family: Theme.fontFamily
                                    font.pixelSize: 7
                                    font.bold: true
                                }
                            }

                            Item {
                                width: Math.max(0, parent.width - 130)
                                height: 1
                            }

                            Text {
                                width: 50
                                horizontalAlignment: Text.AlignRight
                                text: model.ctimeText || ""
                                color: _mutedText
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
                            font.pixelSize: commentsPage._commentBodyFontSize
                            wrapMode: Text.Wrap
                            lineHeight: 1.22
                            renderType: commentsPage._textRenderType
                            font.hintingPreference: commentsPage._hinting
                            antialiasing: commentsPage._textAA
                        }

                        Row {
                            width: parent.width
                            spacing: 6

                            Rectangle {
                                visible: !!commentsPage.firstPicture(model.pictures)
                                width: 56
                                height: 34
                                radius: 8
                                color: Theme.bgTertiary
                                border.color: Theme.withAlpha(_panelBorder, 0.95)
                                border.width: 1
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: replyPictureSource
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    mipmap: true
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: commentsPage.openCommentImage(commentsPage.firstPicture(model.pictures))
                                }
                            }

                            Components.IconButton {
                                icon: "👍"
                                value: commentsPage.compactCount(model.likes || 0)
                                width: 34
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }

            footer: Column {
                width: replyDetailFlick.width
                spacing: 5

                Rectangle {
                    visible: controller && controller.replyHasMore
                    width: parent.width
                    height: 30
                    radius: 10
                    color: Theme.withAlpha(_panelFill, 0.7)
                    border.color: Theme.withAlpha(_panelBorder, 0.7)
                    border.width: 1

                    Components.IconButton {
                        anchors.centerIn: parent
                        width: 92
                        icon: replyLoadBtnBusy ? "⏳" : "↓"
                        label: replyLoadBtnBusy ? "加载中..." : "更多回复"
                        active: replyLoadBtnBusy
                        property bool replyLoadBtnBusy: controller && controller.commentReplyModel() && controller.commentReplyModel().loading
                        onClicked: {
                            if (!replyLoadBtnBusy && controller) controller.fetchMoreCommentReplies()
                        }
                    }
                }

                Text {
                    visible: controller && controller.commentReplyModel() && controller.commentReplyModel().count === 0 && !commentsPage.isAnyCommentLoading()
                    text: "这条评论还没有回复"
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
                        if (controller) controller.cancelAll()
                    }
                }
            }
        }
    }

    Connections {
        target: controller ? controller.commentModel() : null
        function onLoadingChanged() {
            if (!target || !target.loading) commentsPage.autoLoadingComments = false
        }
    }

    Connections {
        target: controller ? controller.commentReplyModel() : null
        function onLoadingChanged() {
            if (!target || !target.loading) commentsPage.autoLoadingReplies = false
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
                Object.defineProperty(obj, "popStackId", {
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
        initialCommentsTimer.restart()
    }

    onVisibleChanged: {
        if (visible && !initialCommentsRequested) initialCommentsTimer.restart()
    }
}
