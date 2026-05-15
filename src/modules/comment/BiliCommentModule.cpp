#include "modules/comment/BiliCommentModule.h"
#include "BiliController.h"
#include "BiliJsonUtils.h"
#include "BiliModels.h"
#include "BiliNetwork.h"
#include "modules/history/BiliHistoryModule.h"
#include "modules/login/BiliLoginModule.h"
#include "modules/season/BiliSeasonModule.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QRegExp>
#include <QSettings>
#include <QStandardPaths>
#include <QStringListModel>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>
#include <algorithm>
#include <functional>
#include <iostream>

// 由插件文件提供的 Go 服务控制函数
extern bool bili_startApiServer();
extern void bili_stopApiServer();

BiliCommentModule::BiliCommentModule(BiliController *controller)
    : m_controller(controller) {}

// ====== API: 评论 ======

void BiliCommentModule::fetchComments(int page) {
  if (m_controller->m_currentVideo.bvid.isEmpty()) {
    emit m_controller->toastMessage("请先打开一个视频");
    return;
  }
  if (m_controller->m_commentModel->loading())
    return;
  if (m_controller->m_currentVideo.aid <= 0) {
    emit m_controller->toastMessage("视频信息不完整");
    return;
  }

  page = qBound(1, page, 1000);
  m_controller->m_commentPage = page;
  if (page == 1) {
    m_controller->m_commentModel->clear();
  }
  m_controller->m_commentModel->setLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_controller->m_currentVideo.aid);
  params["type"] = "1";
  params["sort"] = "2";
  params["pn"] = QString::number(page);
  params["ps"] = "10";

  m_controller->apiGet(
      "/video/comments", params,
      [this](const QJsonObject &data) {

        QJsonObject pageObj = data.value("page").toObject();
        int total = pageObj.value("count").toInt();
        m_controller->m_commentModel->setTotalCount(total);

        QJsonArray replies = data.value("replies").toArray();

        QVector<CommentItem> items;

        // 置顶评论（可能存在 upper/admin/vote 等）
        if (m_controller->m_commentPage == 1) {
          QJsonObject topObj = data.value("top").toObject();
          QStringList topKeys = {"upper", "admin", "vote"};
          for (const QString &key : topKeys) {
            QJsonObject topItem = topObj.value(key).toObject();
            if (!topItem.isEmpty()) {
              CommentItem topComment = CommentListModel::parseCommentItem(topItem);
              topComment.isTop = true;
              items.append(topComment);
            }
          }
        }

        items.reserve(items.size() + replies.size());
        for (const QJsonValue &v : replies) {
          if (v.isObject()) {
            items.append(CommentListModel::parseCommentItem(v.toObject()));
          }
        }

        m_controller->m_commentModel->appendItems(items);
        m_controller->m_commentModel->setLoading(false);

        if (items.isEmpty() && m_controller->m_commentPage == 1) {
          m_controller->m_commentModel->setErrorMessage("暂无评论");
        }
      },
      [this](int code, const QString &msg) {
        m_controller->m_commentModel->setLoading(false);
        if (code == -404 || msg == "啥都木有") {
          m_controller->m_commentModel->setErrorMessage("暂无评论");
        } else {
          m_controller->m_commentModel->setErrorMessage(msg);
          emit m_controller->toastMessage(QString("评论加载失败：%1").arg(msg));
        }
      });
}

void BiliCommentModule::fetchCommentReplies(qint64 rootRpid) {
  if (m_controller->m_currentVideo.aid <= 0 || rootRpid <= 0) {
    emit m_controller->toastMessage("评论信息不完整");
    return;
  }
  if (m_controller->m_commentReplyModel->loading())
    return;

  m_controller->m_currentCommentRootRpid = rootRpid;
  m_controller->m_commentReplyPage = 1;
  m_controller->m_commentReplyHasMore = false;
  emit m_controller->replyHasMoreChanged();

  m_controller->m_commentReplyModel->clear();
  m_controller->m_commentReplyModel->setLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_controller->m_currentVideo.aid);
  params["type"] = "1";
  params["root"] = QString::number(rootRpid);
  params["ps"] = "20";
  params["pn"] = QString::number(m_controller->m_commentReplyPage);

  m_controller->apiGet(
      "/video/comments/replies", params,
      [this](const QJsonObject &data) {

        QJsonArray replies = data.value("replies").toArray();
        QVector<CommentReplyItem> items;
        items.reserve(replies.size());
        for (const QJsonValue &v : replies) {
          if (v.isObject()) {
            items.append(CommentReplyListModel::parseCommentReplyItem(v.toObject()));
          }
        }

        QJsonObject pageObj = data.value("page").toObject();
        int count = pageObj.value("count").toInt(0);
        int num = pageObj.value("num").toInt(m_controller->m_commentReplyPage);
        int size = pageObj.value("size").toInt(20);
        bool hasMore = (count > 0) ? (num * size < count) : (items.size() >= size);

        m_controller->m_commentReplyHasMore = hasMore;
        emit m_controller->replyHasMoreChanged();

        m_controller->m_commentReplyModel->setItems(items);
        m_controller->m_commentReplyModel->setLoading(false);
        if (items.isEmpty()) {
          m_controller->m_commentReplyModel->setErrorMessage("暂无回复");
        }
      },
      [this](int, const QString &msg) {
        m_controller->m_commentReplyHasMore = false;
        emit m_controller->replyHasMoreChanged();
        m_controller->m_commentReplyModel->setLoading(false);
        m_controller->m_commentReplyModel->setErrorMessage(msg);
        emit m_controller->toastMessage(QString("回复加载失败：%1").arg(msg));
      });
}

void BiliCommentModule::fetchMoreCommentReplies() {
  if (m_controller->m_currentCommentRootRpid <= 0) return;
  if (!m_controller->m_commentReplyHasMore) return;
  if (m_controller->m_commentReplyModel->loading()) return;

  m_controller->m_commentReplyPage++;
  m_controller->m_commentReplyModel->setLoading(true);

  QMap<QString, QString> params;
  params["oid"] = QString::number(m_controller->m_currentVideo.aid);
  params["type"] = "1";
  params["root"] = QString::number(m_controller->m_currentCommentRootRpid);
  params["ps"] = "20";
  params["pn"] = QString::number(m_controller->m_commentReplyPage);

  m_controller->apiGet(
      "/video/comments/replies", params,
      [this](const QJsonObject &data) {

        QJsonArray replies = data.value("replies").toArray();
        QVector<CommentReplyItem> items;
        items.reserve(replies.size());
        for (const QJsonValue &v : replies) {
          if (v.isObject()) {
            items.append(CommentReplyListModel::parseCommentReplyItem(v.toObject()));
          }
        }

        QJsonObject pageObj = data.value("page").toObject();
        int count = pageObj.value("count").toInt(0);
        int num = pageObj.value("num").toInt(m_controller->m_commentReplyPage);
        int size = pageObj.value("size").toInt(20);
        bool hasMore = (count > 0) ? (num * size < count) : (items.size() >= size);

        m_controller->m_commentReplyHasMore = hasMore;
        emit m_controller->replyHasMoreChanged();

        m_controller->m_commentReplyModel->appendItems(items);
        m_controller->m_commentReplyModel->setLoading(false);
      },
      [this](int, const QString &msg) {
        m_controller->m_commentReplyModel->setLoading(false);
        emit m_controller->toastMessage(QString("回复加载失败：%1").arg(msg));
      },
      true);
}

void BiliCommentModule::fetchMoreComments() {
  if (m_controller->m_commentModel->loading())
    return;
  m_controller->m_commentPage++;
  fetchComments(m_controller->m_commentPage);
}

