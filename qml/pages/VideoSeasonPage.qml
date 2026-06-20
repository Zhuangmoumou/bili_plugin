import QtQuick 2.12
import BiliPlugin 1.0
import "../components" as Components
import ".."

Rectangle {
    id: seasonPage
    width: 320
    height: 170
    color: Theme.bgPrimary

    property var controller: null
    property var seasonMid: 0
    property var seasonId: 0
    property string seasonTitle: ""
    property string seasonCover: ""
    property int seasonTotal: 0
    property string currentBvid: ""
    property string loadedKey: ""
    property bool seasonLoadingMore: false
    property bool seasonSortOldestFirst: false
    property var seasonModel: controller ? controller.season.seasonVideoModel() : null
    readonly property bool sortBusy: seasonModel ? seasonModel.loading : false

    signal backClicked()
    signal videoSelected(string bvid)

    function totalText() {
        var total = controller && controller.seasonVideoTotal > 0
            ? controller.seasonVideoTotal
            : seasonTotal
        return total > 0 ? "共 " + total + " 个视频" : ""
    }

    function refresh() {
        var midVal = Number(seasonMid)
        var sidVal = Number(seasonId)
        if (!controller || !midVal || !sidVal || midVal <= 0 || sidVal <= 0) return
        if (seasonModel && seasonModel.loading) return
        var key = midVal + "#" + sidVal + "#" + (seasonSortOldestFirst ? "old" : "new")
        if (loadedKey === key) return
        loadedKey = key
        controller.season.fetchSeasonVideos(midVal, sidVal, 1, 30, seasonSortOldestFirst)
    }

    function selectSort(oldestFirst) {
        if (sortBusy || seasonSortOldestFirst === oldestFirst) return
        seasonSortOldestFirst = oldestFirst
        loadedKey = ""
        seasonLoadingMore = false
        seasonVideoList.contentX = 0
    }

    function toggleSort() {
        selectSort(!seasonSortOldestFirst)
    }

    onSeasonMidChanged: {
        loadedKey = ""
        Qt.callLater(refresh)
    }

    onSeasonIdChanged: {
        loadedKey = ""
        Qt.callLater(refresh)
    }

    onSeasonSortOldestFirstChanged: {
        loadedKey = ""
        Qt.callLater(refresh)
    }

    onVisibleChanged: {
        if (visible) Qt.callLater(refresh)
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.bgPrimary }
            GradientStop { position: 1.0; color: "#0f172a" }
        }
    }

    Components.TitleBar {
        id: titleBar
        title: seasonTitle.length > 0 ? seasonTitle : "合集"
        titleSuffix: seasonPage.totalText()
        showBack: true
        anchors.top: parent.top
        onBackClicked: seasonPage.backClicked()
    }

    Rectangle {
        id: sortTitleButton
        width: 42
        height: titleBar.height
        anchors.top: titleBar.top
        anchors.right: titleBar.right
        anchors.rightMargin: 6
        color: "transparent"
        z: titleBar.z + 1
        opacity: seasonPage.sortBusy ? 0.55 : 1.0

        Rectangle {
            id: sortTitleButtonCore
            anchors.centerIn: parent
            width: 30
            height: 24
            radius: Theme.radiusMedium
            color: sortTitleButtonArea.pressed && !seasonPage.sortBusy
                   ? Theme.withAlpha(Theme.primary, 0.22) : "transparent"

            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            Canvas {
                id: sortTitleIcon
                anchors.centerIn: parent
                width: 18
                height: 18
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.lineWidth = 1.7
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.strokeStyle = Theme.primary
                    ctx.fillStyle = Theme.primary

                    ctx.beginPath()
                    ctx.moveTo(2.5, 4.5)
                    ctx.lineTo(10.5, 4.5)
                    ctx.moveTo(2.5, 9)
                    ctx.lineTo(8.5, 9)
                    ctx.moveTo(2.5, 13.5)
                    ctx.lineTo(6.5, 13.5)
                    ctx.stroke()

                    ctx.beginPath()
                    if (seasonPage.seasonSortOldestFirst) {
                        ctx.moveTo(14, 4)
                        ctx.lineTo(14, 14)
                        ctx.moveTo(10.5, 10.5)
                        ctx.lineTo(14, 14)
                        ctx.lineTo(17.5, 10.5)
                    } else {
                        ctx.moveTo(14, 14)
                        ctx.lineTo(14, 4)
                        ctx.moveTo(10.5, 7.5)
                        ctx.lineTo(14, 4)
                        ctx.lineTo(17.5, 7.5)
                    }
                    ctx.stroke()
                }

                Component.onCompleted: requestPaint()
            }
        }

        MouseArea {
            id: sortTitleButtonArea
            anchors.fill: parent
            enabled: !seasonPage.sortBusy
            onClicked: seasonPage.toggleSort()
        }
    }

    Connections {
        target: seasonPage
        function onSeasonSortOldestFirstChanged() {
            sortTitleIcon.requestPaint()
        }
    }

    ListView {
        id: seasonVideoList
        anchors.top: titleBar.bottom
        anchors.topMargin: 4
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 8
        orientation: ListView.Horizontal
        spacing: 6
        clip: true
        cacheBuffer: 640
        displayMarginBeginning: 160
        displayMarginEnd: 160
        leftMargin: 8
        rightMargin: 8
        model: seasonPage.seasonModel

        onAtXEndChanged: {
            if (!controller) return
            if (!atXEnd) return
            if (seasonVideoList.contentWidth <= seasonVideoList.width + 2) return
            if (seasonPage.seasonLoadingMore) return
            if (seasonVideoList.model && seasonVideoList.model.loading) return
            if (seasonVideoList.model && seasonVideoList.model.hasMore === false) return
            seasonPage.seasonLoadingMore = true
            controller.season.fetchMoreSeasonVideos()
        }

        delegate: Item {
            width: 105
            height: seasonVideoList.height
            property bool current: (model.bvid || "") === seasonPage.currentBvid

            Components.VideoCardCompact {
                anchors.fill: parent
                videoTitle: model.title || ""
                coverUrl: model.pic || ""
                imageActive: seasonPage.visible
                preferOffscreenPlaceholder: controller && controller.videoCardOffscreenPlaceholderEnabled
                upName: model.ownerName || ""
                viewCount: model.views || ""
                durationText: model.durationText || ""
                bvid: model.bvid || ""
                showCollection: model.partCount > 1
                titleScale: 0.9
                subScale: 0.85
                onClicked: {
                    if (!bvid || bvid === seasonPage.currentBvid) return
                    seasonPage.videoSelected(bvid)
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: parent.current
                radius: Theme.radiusMedium
                color: "transparent"
                border.color: Theme.primary
                border.width: 2
                z: 2
            }

            Rectangle {
                visible: parent.current
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 4
                anchors.rightMargin: 4
                width: currentText.implicitWidth + 10
                height: 16
                radius: 8
                color: Theme.primary
                z: 3

                Text {
                    id: currentText
                    anchors.centerIn: parent
                    text: "当前"
                    color: Theme.textOnPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.bold: true
                }
            }
        }

        Row {
            visible: seasonVideoList.count === 0 && controller && controller.isLoading
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Repeater {
                model: 3
                Components.VideoCardCompact {
                    height: seasonVideoList.height
                    placeholder: true
                        titleScale: 0.9
                    subScale: 0.85
                }
            }
        }
    }

    Connections {
        target: seasonPage.seasonModel
        function onLoadingChanged() {
            if (!target || !target.loading) seasonPage.seasonLoadingMore = false
            if (target && !target.loading) Qt.callLater(seasonPage.refresh)
        }
        function onCountChanged() {
            seasonPage.seasonLoadingMore = false
        }
    }

    Text {
        anchors.centerIn: seasonVideoList
        visible: seasonVideoList.count === 0 && controller && !controller.isLoading
        text: "该合集暂无视频"
        color: Theme.textTertiary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Rectangle {
        visible: controller && controller.isLoading && seasonVideoList.count === 0
        anchors.centerIn: seasonVideoList
        width: loadingText.implicitWidth + 18
        height: 22
        radius: 11
        color: Theme.withAlpha(Theme.bgSecondary, 0.95)
        border.color: Theme.borderLight
        border.width: 1

        Text {
            id: loadingText
            anchors.centerIn: parent
            text: "加载中"
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
        }
    }
}
