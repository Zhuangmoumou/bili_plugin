import QtQuick 2.12
import FFmpegPlayer 1.0      // ← 替换 QtMultimedia
import BiliPlugin 1.0
import "../components" as Components
import ".."

// ══════════════════════════════════════════
//  修复：调整整体布局的 z 层级
// ══════════════════════════════════════════

Rectangle {
    id: playerPage
    width: 320
    height: 170
    color: "#000000"

    property var controller: null
    signal backClicked()

    property bool controlsVisible: true
    property bool isPlaying: false
    property real playProgress: 0.0
    property bool progressDragActive: false

    readonly property int btnSize: 36
    readonly property int iconSize: 20
    readonly property int barHeight: 36
    readonly property color accentColor: "#00A1D6"
    readonly property string fontFamily: "Microsoft YaHei"

    // ══════════════════════════════════════════
    //  第1层：视频画面（最底层 z: 0）
    //  使用自定义 FFmpeg VideoPlayer（它自身就是渲染组件）
    // ══════════════════════════════════════════
    Rectangle {
        id: videoContainer
        anchors.fill: parent
        color: "#000000"
        z: 0

        VideoPlayer {
            id: videoPlayer
            anchors.fill: parent
            fillMode: VideoPlayer.PreserveAspectFit

            source: ""

            onSourceChanged: {
                var sourceStr = source.toString();
                if (sourceStr.length > 0 && sourceStr !== previousSource) {
                    console.log("[Player] 开始播放:", sourceStr);
                    previousSource = sourceStr;
                    play();
                }
            }

            onPlaybackStateChanged: {
                isPlaying = (playbackState === VideoPlayer.PlayingState);
                console.log("[Player] 播放状态:", playbackState);

                // 播放开始时隐藏错误和占位
                if (playbackState === VideoPlayer.PlayingState) {
                    errorText.visible = false;
                    placeholderText.visible = false;
                }
            }

            onPositionChanged: {
                if (!progressDragActive && duration > 0) {
                    playProgress = position / duration;
                }
            }

            onErrorOccurred: {
                console.log("[Player] 错误:", error);
                errorText.text = error;
                errorText.visible = true;
            }

            onDurationChanged: {
                console.log("[Player] 时长:", duration, "秒");
            }
        }

        Connections {
            target: controller

            function onDownloadStateChanged() {
                // 下载完成且有临时文件路径时，设置播放源
                if (controller && !controller.isDownloading &&
                    controller.tempVideoPath && controller.tempVideoPath.length > 0) {

                    var path = controller.tempVideoPath;
                if (path.indexOf("://") === -1) {
                    path = "file://" + path;
                }

                // 只有当 source 为空或不同时才更新（避免重复设置）
                if (videoPlayer.source.toString() !== path) {
                    console.log("[Player] 下载完成，设置播放源:", path);
                    videoPlayer.source = path;
                }
                    }
            }
        }

        // 无视频时的占位提示
        Text {
            id: placeholderText
            anchors.centerIn: parent
            visible: {
                if (!videoPlayer.source || videoPlayer.source.toString().length === 0)
                    return true;
                if (videoPlayer.playbackState === VideoPlayer.StoppedState &&
                    videoPlayer.duration <= 0)
                    return true;
                return false;
            }
            text: {
                // ══════════════════════════════════════════
                //  根据下载状态显示不同提示
                // ══════════════════════════════════════════
                if (controller) {
                    if (controller.isDownloading)
                        return "正在下载视频...";
                    if (controller.tempVideoPath && controller.tempVideoPath.length > 0)
                        return "加载中...";
                    return "正在获取播放地址...";
                }
                return "正在初始化...";
            }
            color: "#999999"
            font.family: fontFamily
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.4
        }

        // 错误提示
        Text {
            id: errorText
            anchors.centerIn: parent
            visible: false
            color: "#FF6666"
            font.family: fontFamily
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            width: parent.width - 40
        }
    }

    // ══════════════════════════════════════════
    //  第2层：点击区域（z: 10，在视频上方，控制栏下方）
    // ══════════════════════════════════════════
    MouseArea {
        id: videoClickArea
        anchors.fill: parent
        z: 10
        onClicked: {
            controlsVisible = !controlsVisible;
            if (controlsVisible) hideControlsTimer.restart();
        }
    }

    // ══════════════════════════════════════════
    //  第3层：顶部控制栏（z: 20）
    // ══════════════════════════════════════════
    Rectangle {
        id: topBar
        width: parent.width
        height: barHeight
        anchors.top: parent.top
        z: 20
        visible: controlsVisible
        opacity: controlsVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.85) }
            GradientStop { position: 1.0; color: "transparent" }
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 10
            spacing: 8

            Item {
                width: btnSize
                height: btnSize
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: 30
                    height: 26
                    radius: 4
                    color: backBtnArea.pressed ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                    Canvas {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.strokeStyle = "#FFFFFF";
                            ctx.lineWidth = 2.5;
                            ctx.lineCap = "round";
                            ctx.lineJoin = "round";
                            ctx.beginPath();
                            ctx.moveTo(12, 3);
                            ctx.lineTo(5, 9);
                            ctx.lineTo(12, 15);
                            ctx.stroke();
                        }
                    }
                }

                MouseArea {
                    id: backBtnArea
                    anchors.fill: parent
                    onClicked: {
                        videoPlayer.stop();
                        playerPage.backClicked();
                    }
                }
            }

            Text {
                text: controller ? controller.videoTitle : ""
                color: "#FFFFFF"
                font.family: fontFamily
                font.pixelSize: 13
                font.bold: true
                elide: Text.ElideRight
                width: parent.width - btnSize - 24
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // ══════════════════════════════════════════
    //  第3层：底部控制栏（z: 20）
    // ══════════════════════════════════════════
    Rectangle {
        id: bottomBar
        width: parent.width
        height: barHeight
        anchors.bottom: parent.bottom
        z: 20
        visible: controlsVisible
        opacity: controlsVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }

        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.85) }
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 10
            spacing: 10

            Item {
                width: btnSize
                height: btnSize
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: 30
                    height: 26
                    radius: 4
                    color: playBtnArea.pressed ? Qt.rgba(1, 1, 1, 0.2) : "transparent"

                    Canvas {
                        id: playPauseIcon
                        anchors.centerIn: parent
                        width: iconSize
                        height: iconSize
                        property bool playing: isPlaying
                        onPlayingChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.clearRect(0, 0, width, height);
                            ctx.fillStyle = "#FFFFFF";
                            if (playing) {
                                var barWidth = 5;
                                var barH = 16;
                                var gap = 5;
                                var startX = (width - barWidth * 2 - gap) / 2;
                                var startY = (height - barH) / 2;
                                ctx.fillRect(startX, startY, barWidth, barH);
                                ctx.fillRect(startX + barWidth + gap, startY, barWidth, barH);
                            } else {
                                var triWidth = 14;
                                var triHeight = 16;
                                var offsetX = (width - triWidth) / 2 + 2;
                                var offsetY = (height - triHeight) / 2;
                                ctx.beginPath();
                                ctx.moveTo(offsetX, offsetY);
                                ctx.lineTo(offsetX, offsetY + triHeight);
                                ctx.lineTo(offsetX + triWidth, offsetY + triHeight / 2);
                                ctx.closePath();
                                ctx.fill();
                            }
                        }
                        Component.onCompleted: requestPaint()
                    }
                }

                MouseArea {
                    id: playBtnArea
                    anchors.fill: parent
                    onClicked: {
                        videoPlayer.togglePlayPause();  // ← 使用新方法
                        hideControlsTimer.restart();
                    }
                }
            }

            Text {
                id: currentTimeText
                text: formatTime(videoPlayer.position)  // ← 直接使用秒
                color: "#FFFFFF"
                font.family: fontFamily
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                function formatTime(seconds) {
                    var mins = Math.floor(seconds / 60);
                    var secs = Math.floor(seconds % 60);
                    return mins + ":" + (secs < 10 ? "0" : "") + secs;
                }
            }

            Item {
                id: progressArea
                width: parent.width - btnSize - currentTimeText.width - totalTimeText.width - 46
                height: btnSize
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    id: progressTrack
                    width: parent.width
                    height: 6
                    radius: 3
                    color: Qt.rgba(1, 1, 1, 0.3)
                    anchors.verticalCenter: parent.verticalCenter

                    Rectangle {
                        width: parent.width * playProgress
                        height: parent.height
                        radius: 3
                        color: accentColor
                        Behavior on width { NumberAnimation { duration: 50 } }
                    }

                    Rectangle {
                        id: progressHandle
                        x: Math.max(0, Math.min(parent.width * playProgress - width / 2, parent.width - width))
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        radius: 7
                        color: accentColor
                        border.color: "#FFFFFF"
                        border.width: 2
                        Behavior on x { NumberAnimation { duration: 50 } }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.topMargin: -10
                    anchors.bottomMargin: -10

                    onPressed: {
                        progressDragActive = true;
                        var pos = Math.max(0, Math.min(mouse.x, progressTrack.width));
                        playProgress = pos / progressTrack.width;
                    }
                    onPositionChanged: {
                        if (pressed) {
                            var pos = Math.max(0, Math.min(mouse.x, progressTrack.width));
                            playProgress = pos / progressTrack.width;
                        }
                    }
                    onReleased: {
                        // VideoPlayer.seek() 使用秒
                        var targetSec = playProgress * videoPlayer.duration;
                        videoPlayer.seek(targetSec);
                        progressDragActive = false;
                        hideControlsTimer.restart();
                    }
                    onClicked: {
                        var pos = Math.max(0, Math.min(mouse.x, progressTrack.width));
                        playProgress = pos / progressTrack.width;
                        var targetSec = playProgress * videoPlayer.duration;
                        videoPlayer.seek(targetSec);
                        progressDragActive = false;
                        hideControlsTimer.restart();
                    }
                }
            }

            Text {
                id: totalTimeText
                text: formatTotalTime(videoPlayer.duration)  // ← 直接使用秒
                color: Qt.rgba(1, 1, 1, 0.7)
                font.family: fontFamily
                font.pixelSize: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                function formatTotalTime(seconds) {
                    if (seconds <= 0) return "0:00";
                    var mins = Math.floor(seconds / 60);
                    var secs = Math.floor(seconds % 60);
                    return mins + ":" + (secs < 10 ? "0" : "") + secs;
                }
            }
        }
    }

    // ══════════════════════════════════════════
    //  第4层：下载进度（z: 100，最顶层）
    // ══════════════════════════════════════════
    Rectangle {
        id: downloadProgressOverlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.8, 300)
        height: 80
        radius: 8
        color: Qt.rgba(0, 0, 0, 0.85)
        visible: controller && controller.isDownloading
        z: 100

        Column {
            anchors.centerIn: parent
            width: parent.width - 20
            spacing: 8

            Text {
                text: controller ? controller.downloadStatus : "正在下载..."
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
                    color: accentColor
                }
            }
        }

        // 点击任意位置取消下载
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (controller) {
                    controller.cancelDownload();
                }
            }
        }
    }

    // ══════════════════════════════════════════
    //  Loading 指示器
    // ══════════════════════════════════════════
    Components.LoadingIndicator {
        anchors.centerIn: parent
        z: 50
        running: controller && controller.isLoading && !controller.isDownloading
        message: controller && controller.isDownloading ? "正在下载视频..." : "获取播放地址..."
    }

    // ══════════════════════════════════════════
    //  自动隐藏计时器
    // ══════════════════════════════════════════
    Timer {
        id: hideControlsTimer
        interval: 3500
        onTriggered: controlsVisible = false
    }

    onIsPlayingChanged: {
        if (isPlaying) {
            hideControlsTimer.restart();
        } else {
            hideControlsTimer.stop();
            controlsVisible = true;
        }
    }

    Component.onCompleted: {
        if (controller) controller.downloadAndPlay(16);
        hideControlsTimer.start();
    }

    Component.onDestruction: {
        videoPlayer.stop();
        videoPlayer.source = "";
        if (controller) controller.cleanupTempVideo();
    }
}
