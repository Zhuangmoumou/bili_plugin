import QtQuick 2.12
import BiliPlugin 1.0
import "pages" as Pages
import "components" as Components

Rectangle {
    id: root
    width: 320
    height: 170
    color: Theme.bgPrimary
    clip: true

    signal backButtonClicked()

    BiliController {
        id: controller
    }

    property var rootController: controller

    // ── 页面管理 ──
    property string currentPage: "home"
    property var pageStack: []
    property string detailBvid: ""
    property string lastPage: "home"
    property bool _animating: false

    // 首页推荐列表滚动位置缓存（仅从详情页返回时恢复）
    property real homePopularX: 0
    property bool restoreHomePopularOnShow: false

    function navigateTo(page, props) {
        if (_animating) return;
        var newStack = pageStack.slice(0); // Create a copy
        newStack.push(currentPage);
        pageStack = newStack; // Assign the new array
        lastPage = currentPage;
        if (props) {
            if (props.bvid) detailBvid = props.bvid;
        }
        _animating = true;
        currentPage = page;
        pageTransition.restart();
    }

    function goBack() {
        if (_animating) return;
        if (pageStack.length > 0) {
            var newStack = pageStack.slice(0); // Create a copy
            var prev = newStack.pop();
            var fromPage = currentPage;
            pageStack = newStack; // Assign the new array
            _animating = true;
            currentPage = prev;

            // 从详情页返回首页时恢复推荐列表滚动位置
            if (fromPage === "detail" && prev === "home") {
                restoreHomePopularOnShow = true;
            }

            pageTransitionBack.restart();
        } else {
            if (currentPage === "comments" && detailBvid.length > 0) {
                _animating = true;
                currentPage = "detail";
                pageTransitionBack.restart();
                return;
            }
            if (currentPage !== "home") {
                _animating = true;
                currentPage = lastPage && lastPage !== currentPage ? lastPage : "home";
                pageTransitionBack.restart();
                return;
            }
            backButtonClicked();
        }
    }

    // ── 页面切换动画容器 ──
    Item {
        id: pageContainer
        anchors.fill: parent
        opacity: 1
        transform: Translate { id: pageTranslate; x: 0 }

        SequentialAnimation {
            id: pageTransition
            NumberAnimation {
                target: pageContainer; property: "opacity"
                from: 1; to: 0; duration: Theme.animFast
                easing.type: Easing.OutQuad
            }
            NumberAnimation {
                target: pageTranslate; property: "x"
                from: 30; to: 0; duration: Theme.animNormal
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: pageContainer; property: "opacity"
                from: 0; to: 1; duration: Theme.animFast
                easing.type: Easing.InQuad
            }
            onFinished: root._animating = false
        }

        SequentialAnimation {
            id: pageTransitionBack
            NumberAnimation {
                target: pageContainer; property: "opacity"
                from: 1; to: 0; duration: Theme.animFast
                easing.type: Easing.OutQuad
            }
            NumberAnimation {
                target: pageTranslate; property: "x"
                from: -30; to: 0; duration: Theme.animNormal
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: pageContainer; property: "opacity"
                from: 0; to: 1; duration: Theme.animFast
                easing.type: Easing.InQuad
            }
            onFinished: root._animating = false
        }

        // ── 页面加载器 ──
        Loader {
            id: homeLoader
            active: currentPage === "home"
            anchors.fill: parent
            sourceComponent: Component {
                Pages.HomePage {
                    id: homePage
                    controller: root.rootController
                    rootRef: root
                    onVideoSelected: {
                        if (homeLoader.item) {
                            root.homePopularX = homeLoader.item.popularContentX();
                        }
                        root.navigateTo("detail", { bvid: bvid })
                    }
                    onSearchRequested: root.navigateTo("search")
                    onLoginRequested: root.navigateTo("user")
                }
            }
        }

        Loader {
            id: searchLoader
            // 仅在搜索页或从搜索进入详情页时保持实例
            active: currentPage === "search" || (currentPage === "detail" && lastPage === "search")
            visible: currentPage === "search"
            anchors.fill: parent
            sourceComponent: Component {
                Pages.SearchPage {
                    controller: root.rootController
                    onBackClicked: {
                        root.rootController.searchModel().clear();
                        root.goBack();
                    }
                    onVideoSelected: root.navigateTo("detail", { bvid: bvid })
                }
            }
        }

        Loader {
            active: currentPage === "detail"
            anchors.fill: parent
            sourceComponent: Component {
                Pages.VideoDetailPage {
                    controller: root.rootController
                    bvid: root.detailBvid
                    onBackClicked: root.goBack()
                    onPlayRequested: root.navigateTo("player")
                    onCommentsRequested: root.navigateTo("comments")
                }
            }
        }

        Loader {
            active: currentPage === "player"
            anchors.fill: parent
            sourceComponent: Component {
                Pages.PlayerPage {
                    controller: root.rootController
                    onBackClicked: root.goBack()
                }
            }
        }

        Loader {
            active: currentPage === "comments"
            anchors.fill: parent
            sourceComponent: Component {
                Pages.CommentsPage {
                    controller: root.rootController
                    onBackClicked: root.goBack()
                }
            }
        }

        Loader {
            active: currentPage === "ranking"
            anchors.fill: parent
            sourceComponent: Component {
                Pages.RankingPage {
                    controller: root.rootController
                    onBackClicked: root.goBack()
                    onVideoSelected: root.navigateTo("detail", { bvid: bvid })
                }
            }
        }

        Loader {
            active: currentPage === "user"
            anchors.fill: parent
            sourceComponent: Component {
                Pages.UserPage {
                    controller: root.rootController
                    onBackClicked: root.goBack()
                }
            }
        }
    }

    // ── 全局错误遮罩 ──
    Components.ErrorOverlay {
        anchors.fill: parent
        errorMessage: controller.globalError
        onRetryClicked: {
            controller.clearError();
            if (currentPage === "home") controller.fetchPopular();
            else if (currentPage === "detail") controller.fetchVideoDetail(root.detailBvid);
        }
        onDismissed: controller.clearError()
    }

    // ── Toast ──
    Components.Toast {
        id: globalToast
    }

    Timer {
        id: loginSuccessTimer
        interval: 3000  // 等待 3 秒，让用户信息完全加载并保存
        running: false
        onTriggered: {
            if (controller && controller.loggedIn && root.currentPage === "user") {
                console.log("[UserPage] Timer expired, going back to home");
                root.goBack();
            }
        }
    }

    Connections {
        target: controller
        function onToastMessage(message) { globalToast.show(message); }
        function onQrcodeLoginSuccess() {
            console.log("[UserPage] QR login success, waiting for user info...");
            globalToast.show("登录成功！🎉", 3000);
            // 延迟返回，等待 checkLoginStatus() 回调更新用户信息
            loginSuccessTimer.start();
        }
        function onQrcodeNeedRefresh() {
            globalToast.show("二维码已过期，请重新获取", 2500);
        }
        function onLoginStateChanged() {
            console.log("[UserPage] loginStateChanged:", controller.loggedIn, "userName:", controller.userName);
        }
    }

    Component.onCompleted: {
        console.log("=== BiliPlugin Loaded ===", width, "x", height);
        // 仅首次进入插件时加载推荐（fresh_type=3）
        if (controller) controller.fetchPopular();
    }
}
