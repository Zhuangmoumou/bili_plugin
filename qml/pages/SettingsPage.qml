import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Item {
    id: settingsPage
    anchors.fill: parent

    property var controller: null
    // 4=设置列表, 5=字幕设置
    property int viewMode: 4

    signal requestSubtitleSettings()

    property bool restartConfirmVisible: false

    // ── 设置页 ──
    Item {
        id: settingsView
        anchors.fill: parent
        visible: viewMode === 4

        ListView {
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            model: ListModel {
                ListElement { title: "字幕设置"; action: "subtitle" }
                ListElement { title: "重启 Go 服务端"; action: "restart" }
            }
            spacing: Theme.spacingSmall
            clip: true

            delegate: Rectangle {
                width: parent.width
                height: 36
                radius: Theme.radiusMedium
                color: settingsItemArea.pressed ? Theme.withAlpha(Theme.primary, 0.12) : Theme.bgSecondary
                border.color: Theme.withAlpha(Theme.primary, 0.12)
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: model.title
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: settingsItemArea
                    anchors.fill: parent
                    onClicked: {
                        if (model.action === "subtitle") {
                            settingsPage.requestSubtitleSettings()
                        } else {
                            settingsPage.restartConfirmVisible = true
                        }
                    }
                }
            }
        }
    }

    // ── 字幕设置 ──
    Item {
        id: subtitleSettingsView
        anchors.fill: parent
        visible: viewMode === 5

        Flickable {
            anchors.fill: parent
            anchors.margins: Theme.spacingSmall
            contentHeight: subtitleSettingsColumn.height
            clip: true
            boundsBehavior: Flickable.DragOverBounds

            Column {
                id: subtitleSettingsColumn
                width: parent.width
                spacing: Theme.spacingSmall

                function round1(v) { return Math.round(v * 10) / 10 }

                Rectangle {
                    width: parent.width
                    height: 30
                    radius: Theme.radiusMedium
                    color: Theme.bgSecondary
                    border.color: Theme.withAlpha(Theme.primary, 0.12)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Text { text: "字体大小"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 64 }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: fontMinusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "-"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: fontMinusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleFontSize(controller.subtitleFontSize - 1) }
                            }
                        }

                        Text { text: controller ? controller.subtitleFontSize : 0; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 30; horizontalAlignment: Text.AlignHCenter }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: fontPlusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "+"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: fontPlusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleFontSize(controller.subtitleFontSize + 1) }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 30
                    radius: Theme.radiusMedium
                    color: Theme.bgSecondary
                    border.color: Theme.withAlpha(Theme.primary, 0.12)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Text { text: "字粗"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 64 }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: boldMinusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "-"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: boldMinusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleBold(controller.subtitleBold - 1) }
                            }
                        }

                        Text { text: controller ? (controller.subtitleBold === 1 ? "粗" : "细") : "细"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 40; horizontalAlignment: Text.AlignHCenter }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: boldPlusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "+"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: boldPlusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleBold(controller.subtitleBold + 1) }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 30
                    radius: Theme.radiusMedium
                    color: Theme.bgSecondary
                    border.color: Theme.withAlpha(Theme.primary, 0.12)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Text { text: "描边粗细"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 64 }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: outlineMinusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "-"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: outlineMinusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleOutline(subtitleSettingsColumn.round1(controller.subtitleOutline - 0.1)) }
                            }
                        }

                        Text { text: controller ? subtitleSettingsColumn.round1(controller.subtitleOutline).toFixed(1) : "0.0"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 40; horizontalAlignment: Text.AlignHCenter }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: outlinePlusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "+"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: outlinePlusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleOutline(subtitleSettingsColumn.round1(controller.subtitleOutline + 0.1)) }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 30
                    radius: Theme.radiusMedium
                    color: Theme.bgSecondary
                    border.color: Theme.withAlpha(Theme.primary, 0.12)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Text { text: "底边距离"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 64 }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: marginMinusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "-"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: marginMinusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleMarginV(controller.subtitleMarginV - 1) }
                            }
                        }

                        Text { text: controller ? controller.subtitleMarginV : 0; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 30; horizontalAlignment: Text.AlignHCenter }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: marginPlusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "+"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: marginPlusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleMarginV(controller.subtitleMarginV + 1) }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 30
                    radius: Theme.radiusMedium
                    color: Theme.bgSecondary
                    border.color: Theme.withAlpha(Theme.primary, 0.12)
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Text { text: "字间距"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 64 }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: spacingMinusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "-"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: spacingMinusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleSpacing(subtitleSettingsColumn.round1(controller.subtitleSpacing - 0.1)) }
                            }
                        }

                        Text { text: controller ? subtitleSettingsColumn.round1(controller.subtitleSpacing).toFixed(1) : "0.0"; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: 11; width: 40; horizontalAlignment: Text.AlignHCenter }

                        Rectangle {
                            width: 24; height: 24; radius: 6
                            color: spacingPlusArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : Theme.bgTertiary
                            Text { anchors.centerIn: parent; text: "+"; color: Theme.textPrimary; font.pixelSize: 14 }
                            MouseArea {
                                id: spacingPlusArea
                                anchors.fill: parent
                                onClicked: { if (controller) controller.setSubtitleSpacing(subtitleSettingsColumn.round1(controller.subtitleSpacing + 0.1)) }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── 重启确认 ──
    Rectangle {
        anchors.fill: parent
        visible: restartConfirmVisible
        color: Qt.rgba(0, 0, 0, 0.55)
        z: 92

        Rectangle {
            width: 170
            height: 70
            radius: 10
            color: Theme.bgSecondary
            border.color: Theme.withAlpha(Theme.primary, 0.2)
            border.width: 1
            anchors.centerIn: parent

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Text {
                    text: "确认重启 Go 服务端？"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 10

                    Rectangle {
                        width: 56
                        height: 24
                        radius: 6
                        color: cancelRestartArea.pressed ? Theme.withAlpha(Theme.primary, 0.12) : Theme.bgTertiary

                        Text {
                            anchors.centerIn: parent
                            text: "取消"
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }

                        MouseArea {
                            id: cancelRestartArea
                            anchors.fill: parent
                            onClicked: settingsPage.restartConfirmVisible = false
                        }
                    }

                    Rectangle {
                        width: 56
                        height: 24
                        radius: 6
                        color: confirmRestartArea.pressed ? Theme.primaryDark : Theme.primary

                        Text {
                            anchors.centerIn: parent
                            text: "确认"
                            color: Theme.textOnPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                        }

                        MouseArea {
                            id: confirmRestartArea
                            anchors.fill: parent
                            onClicked: {
                                settingsPage.restartConfirmVisible = false
                                if (controller) controller.restartGoServer()
                            }
                        }
                    }
                }
            }
        }
    }
}
