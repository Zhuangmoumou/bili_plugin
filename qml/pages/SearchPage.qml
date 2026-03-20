import QtQuick 2.12
import BiliPlugin 1.0
import "qrc:/qml/commons"
import "../components" as Components
import ".."

Rectangle {
    id: searchPage
    width: 320
    height: 170
    color: Theme.bgPrimary

    property var controller: null
    readonly property string fontFamily: "Microsoft YaHei"  // 微软雅黑

    signal backClicked()
    signal videoSelected(string bvid)

    // ═══════════════════════════════════════════════════════════
    // 虚拟键盘调用
    // ═══════════════════════════════════════════════════════════
    function requestKeyboard() {
        let component = qmlCreateComponent("YInputPage");
        if (Component.Ready === component.status) {
            var incubator = component.incubateObject(id_page_pop_helper.containerItem);
            if (incubator.status !== Component.Ready) {
                incubator.onStatusChanged = function(status) {
                    if (status === Component.Ready)
                        id_page_pop_helper.inputPageCreated(incubator.object);
                };
            } else {
                id_page_pop_helper.inputPageCreated(incubator.object);
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 搜索栏
    // ═══════════════════════════════════════════════════════════
    Rectangle {
        id: searchBar
        width: parent.width
        height: 38
        color: Theme.bgSecondary
        z: 10

        Row {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 6

            // ── 返回按钮 ──
            Item {
                width: 36
                height: 36
                anchors.verticalCenter: parent.verticalCenter

                Rectangle {
                    anchors.centerIn: parent
                    width: 30
                    height: 30
                    radius: 8
                    color: backBtnArea.pressed
                    ? Qt.rgba(0.15, 0.56, 0.94, 0.15)
                    : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: 100 }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "‹"
                        color: Theme.primary
                        font.family: searchPage.fontFamily
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                }

                MouseArea {
                    id: backBtnArea
                    anchors.fill: parent
                    onClicked: searchPage.backClicked()
                }
            }

            // ── 搜索输入框 ──
            Rectangle {
                id: searchInputBox
                width: parent.width - 36 - 52 - 18
                height: 30
                radius: 15
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.bgInput
                border.width: 1.5
                border.color: searchInputArea.pressed
                ? Theme.primary
                : Qt.rgba(0, 0, 0, 0.06)

                Behavior on border.color {
                    ColorAnimation { duration: 150 }
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    spacing: 8

                    // 搜索图标
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "🔍"
                        font.pixelSize: 11
                        opacity: 0.4
                    }

                    // 显示文本
                    Text {
                        id: displayText
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 30
                        text: searchInput.text.length > 0
                        ? searchInput.text
                        : "搜索视频、UP主..."
                        color: searchInput.text.length > 0
                        ? Theme.textPrimary
                        : Theme.textTertiary
                        font.family: searchPage.fontFamily
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }

                // 清除按钮
                Rectangle {
                    visible: searchInput.text.length > 0
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18
                    height: 18
                    radius: 9
                    color: clearBtnArea.pressed
                    ? Qt.rgba(0, 0, 0, 0.15)
                    : Qt.rgba(0, 0, 0, 0.08)

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: Theme.textSecondary
                        font.family: searchPage.fontFamily
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }

                    MouseArea {
                        id: clearBtnArea
                        anchors.fill: parent
                        onClicked: {
                            searchInput.text = "";
                            showResults = false;
                        }
                    }
                }

                // 隐藏的 TextInput 用于存储
                TextInput {
                    id: searchInput
                    visible: false
                }

                // 点击触发键盘
                MouseArea {
                    id: searchInputArea
                    anchors.fill: parent
                    anchors.rightMargin: searchInput.text.length > 0 ? 26 : 0
                    onClicked: requestKeyboard()
                }
            }

            // ── 搜索按钮 ──
            Rectangle {
                width: 52
                height: 30
                radius: 15
                anchors.verticalCenter: parent.verticalCenter
                color: searchBtnArea.pressed
                ? Theme.primaryDark
                : Theme.primary

                Behavior on color {
                    ColorAnimation { duration: 80 }
                }

                scale: searchBtnArea.pressed ? 0.95 : 1.0
                Behavior on scale {
                    NumberAnimation { duration: 80 }
                }

                Text {
                    anchors.centerIn: parent
                    text: "搜索"
                    color: "#FFFFFF"
                    font.family: searchPage.fontFamily
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }

                MouseArea {
                    id: searchBtnArea
                    anchors.fill: parent
                    onClicked: doSearch()
                }
            }
        }

        // 底部分隔线
        Rectangle {
            width: parent.width
            height: 1
            anchors.bottom: parent.bottom
            color: Theme.divider
            opacity: 0.6
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 搜索逻辑
    // ═══════════════════════════════════════════════════════════
    property bool showResults: false
    property real savedResultContentX: 0

    function restoreSearchPosition() {
        if (!showResults) return;
        if (searchResultList.count <= 0) return;
        if (savedResultContentX <= 0) return;
        Qt.callLater(function() {
            searchResultList.contentX = savedResultContentX;
        });
    }

    function doSearch() {
        var kw = searchInput.text.trim();
        if (kw.length === 0) return;
        var searchModel = controller ? controller.searchModel() : null;
        if (searchModel && searchModel.keyword === kw && searchModel.count > 0) {
            showResults = true;
            restoreSearchPosition();
            return;
        }
        savedResultContentX = 0;
        searchResultList.contentX = 0;
        if (controller) controller.search(kw);
        showResults = true;
    }

    // ═══════════════════════════════════════════════════════════
    // 热搜列表
    // ═══════════════════════════════════════════════════════════
    Flickable {
        id: hotSearchArea
        visible: !showResults
        anchors {
            top: searchBar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            topMargin: 4
            leftMargin: 8
            rightMargin: 8
            bottomMargin: 4
        }
        contentHeight: hotSearchCol.height
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: hotSearchCol
            width: parent.width
            spacing: 2

            // ─────────────────────────────────────
            // 搜索历史
            // ─────────────────────────────────────
            Column {
                id: historyColumn
                width: parent.width
                spacing: 2
                visible: searchInput.text.length === 0 && historyRepeater.count > 0

                // 历史记录标题
                Item {
                    width: parent.width
                    height: 24
                
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 4
                
                        Text {
                            text: "⏳"
                            font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: "搜索历史"
                            color: Theme.textSecondary
                            font.family: searchPage.fontFamily
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                
                    Rectangle {
                        id: clearHistoryBtn
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        height: 18
                        width: clearHistoryText.implicitWidth + 10
                        radius: 9
                        visible: historyRepeater.count > 0
                        color: clearHistoryArea.pressed
                               ? Qt.rgba(0, 0, 0, 0.14)
                               : Qt.rgba(0, 0, 0, 0.08)
                
                        Text {
                            id: clearHistoryText
                            anchors.centerIn: parent
                            text: "🗑清空"
                            color: Theme.textSecondary
                            font.family: searchPage.fontFamily
                            font.pixelSize: 10
                        }
                
                        MouseArea {
                            id: clearHistoryArea
                            anchors.fill: parent
                            anchors.margins: -4
                            onClicked: {
                                if (controller) controller.clearSearchHistory()
                            }
                        }
                    }
                }

                // 历史记录项 (横向流动布局)
                Flow {
                    width: parent.width
                    spacing: 6
                    
                    Repeater {
                        id: historyRepeater
                        model: controller ? controller.searchHistoryModel() : null

                        Rectangle {
                            height: 24
                            width: childrenRect.width + 16
                            radius: 12
                            color: historyItemArea.pressed ? Qt.rgba(0,0,0,0.1) : Qt.rgba(0,0,0,0.05)
                            border.width: 1
                            border.color: Qt.rgba(0,0,0,0.08)

                            Text {
                                anchors.centerIn: parent
                                text: model.display
                                color: Theme.textSecondary
                                font.family: searchPage.fontFamily
                                font.pixelSize: 12
                            }

                            MouseArea {
                                id: historyItemArea
                                anchors.fill: parent
                                onClicked: {
                                    searchInput.text = model.display;
                                    doSearch();
                                }
                            }
                        }
                    }
                }
            }

            // ─────────────────────────────────────
            // 热搜榜
            // ─────────────────────────────────────
            // 标题
            Row {
                spacing: 4
                leftPadding: 4
                bottomPadding: 2

                Text {
                    text: "🔥"
                    font.pixelSize: 11
                }
                Text {
                    text: "热搜榜"
                    color: Theme.textSecondary
                    font.family: searchPage.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            // 热搜项目
            Repeater {
                model: controller ? controller.hotSearchModel() : null

                Rectangle {
                    width: hotSearchCol.width
                    height: 26
                    radius: 6
                    color: hotItemArea.pressed
                    ? Qt.rgba(0.15, 0.56, 0.94, 0.1)
                    : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: 80 }
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        spacing: 10

                        // 排名标签
                        Rectangle {
                            width: 18
                            height: 18
                            radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: {
                                if (index === 0) return "#FF6B6B";
                                if (index === 1) return "#FFA94D";
                                if (index === 2) return "#FFD43B";
                                return Qt.rgba(0, 0, 0, 0.05);
                            }

                            Text {
                                anchors.centerIn: parent
                                text: (index + 1).toString()
                                color: index < 3 ? "#FFFFFF" : Theme.textTertiary
                                font.family: searchPage.fontFamily
                                font.pixelSize: 10
                                font.weight: Font.Bold
                            }
                        }

                        // 关键词
                        Text {
                            text: model.keyword || ""
                            color: Theme.textPrimary
                            font.family: searchPage.fontFamily
                            font.pixelSize: 13
                            elide: Text.ElideRight
                            width: hotSearchCol.width - 44
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    MouseArea {
                        id: hotItemArea
                        anchors.fill: parent
                        onClicked: {
                            searchInput.text = model.keyword;
                            doSearch();
                        }
                    }
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 搜索结果
    // ═══════════════════════════════════════════════════════════
    ListView {
        id: searchResultList
        visible: showResults
        anchors {
            top: searchBar.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            margins: 6
        }
        model: controller ? controller.searchModel() : null
        orientation: ListView.Horizontal
        spacing: 10
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        delegate: Components.VideoCard {
            height: searchResultList.height
            videoTitle: model.title || ""
            coverUrl: model.pic || ""
            upName: model.ownerName || ""
            viewCount: model.views || ""
            durationText: model.durationText || ""
            bvid: model.bvid || ""
            onClicked: {
                searchPage.savedResultContentX = searchResultList.contentX
                searchPage.videoSelected(bvid)
            }
        }

        onAtXEndChanged: {
            if (atXEnd && controller) controller.searchMore();
        }

        onCountChanged: {
            if (searchPage.showResults) searchPage.restoreSearchPosition();
        }

        // ── 空状态提示 ──
        Column {
            visible: searchResultList.count === 0
            && showResults
            && controller
            && !controller.isLoading
            anchors.centerIn: parent
            spacing: 6

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "📭"
                font.pixelSize: 24
                opacity: 0.4
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: {
                    var sm = controller ? controller.searchModel() : null;
                    if (sm && sm.errorMessage) return sm.errorMessage;
                    return "未找到相关视频";
                }
                color: Theme.textTertiary
                font.family: searchPage.fontFamily
                font.pixelSize: 12
            }

            // 返回热搜按钮
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 80
                height: 26
                radius: 13
                color: retryBtnArea.pressed
                ? Qt.rgba(0, 0, 0, 0.08)
                : Qt.rgba(0, 0, 0, 0.04)
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.1)

                Text {
                    anchors.centerIn: parent
                    text: "返回热搜"
                    color: Theme.textSecondary
                    font.family: searchPage.fontFamily
                    font.pixelSize: 11
                }

                MouseArea {
                    id: retryBtnArea
                    anchors.fill: parent
                    onClicked: {
                        searchInput.text = "";
                        showResults = false;
                    }
                }
            }
        }
    }

    YPagePopHelper {
        id: id_page_pop_helper
        z: 99

        function inputPageCreated(keyboardPage) {
            keyboardPage.backButtonClicked.connect(function() {
                qmlGlobal.inputPageShowing = false;
                keyboardPage.todoDestroy();
                keyboardPage = null;
            });

            keyboardPage.inputFinished.connect(function(content) {
                searchInput.text = content.trim();
                qmlGlobal.inputPageShowing = false;
                keyboardPage.todoDestroy();
                if (searchInput.text.length > 0) {
                    doSearch();
                }
            });

            keyboardPage.enterText(searchInput.text);
            keyboardPage.show();
            qmlGlobal.inputPageShowing = true;
        }

        isShowing: qmlGlobal.inputPageShowing
        objectName: "from_SearchPage.qml"
    }

    // ═══════════════════════════════════════════════════════════
    // 加载指示器
    // ═══════════════════════════════════════════════════════════
    Components.LoadingIndicator {
        anchors.centerIn: parent
        running: controller ? controller.isLoading : false
    }

    // ═══════════════════════════════════════════════════════════
    // 初始化
    // ═══════════════════════════════════════════════════════════
    Component.onCompleted: {
        if (controller) {
            // 返回搜索页时，如果已有结果就直接恢复，不再触发新的请求
            var searchModel = controller.searchModel();
            if (searchModel && searchModel.keyword && searchModel.count > 0) {
                searchInput.text = searchModel.keyword;
                showResults = true;
                restoreSearchPosition();
            } else {
                controller.fetchHotSearch();
            }
        }
    }

    onVisibleChanged: {
        if (visible) restoreSearchPosition();
    }

    onShowResultsChanged: {
        if (showResults) restoreSearchPosition();
    }
}
