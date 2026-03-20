import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Rectangle {
    id: commentsPage
    width: 320
    height: 170
    color: Theme.bgPrimary

    property var controller: null
    signal backClicked()

    Components.TitleBar {
        id: titleBar
        title: {
            var cm = controller ? controller.commentModel() : null;
            var total = cm ? cm.totalCount : 0;
            return "评论" + (total > 0 ? " (" + total + ")" : "");
        }
        showBack: true
        anchors.top: parent.top
        onBackClicked: commentsPage.backClicked()
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
                            }
                        }

                        Text {
                            text: model.ctimeText || ""
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTiny
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

            Rectangle {
                anchors.centerIn: parent
                width: 90; height: 20
                radius: Theme.radiusRound
                color: loadMoreArea.pressed
                ? Theme.withAlpha(Theme.primary, 0.15) : "transparent"
                border.color: Theme.borderLight
                border.width: 1

                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    anchors.centerIn: parent
                    text: {
                        var cm = controller ? controller.commentModel() : null;
                        if (cm && cm.loading) return "加载中...";
                        return "加载更多 ↓";
                    }
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                }

                MouseArea {
                    id: loadMoreArea
                    anchors.fill: parent
                    anchors.margins: -4
                    onClicked: {
                        if (controller) controller.fetchMoreComments();
                    }
                }
            }
        }

        // 空状态
        Column {
            visible: commentList.count === 0 && controller && !controller.isLoading
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

    Components.LoadingIndicator {
        anchors.centerIn: parent
        running: controller ? controller.isLoading : false
    }

    Component.onCompleted: {
        if (controller) controller.fetchComments();
    }
}
