#include "modules/video/BiliVideoModule.h"
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

BiliVideoModule::BiliVideoModule(BiliController *controller)
    : m_controller(controller) {}

// ====== API: 视频详情 ======

void BiliVideoModule::reportCurrentVideoAsRecentViewIfNeeded() {
  if (!m_controller->m_loggedIn)
    return;
  if (m_controller->m_currentVideo.aid <= 0 || m_controller->m_currentVideo.cid <= 0 || m_controller->m_currentVideo.bvid.isEmpty())
    return;

  QString reportKey = m_controller->m_currentVideo.bvid + "#" + QString::number(m_controller->m_currentVideo.cid);
  if (m_controller->m_lastRecentViewReportKey == reportKey)
    return;
  m_controller->m_lastRecentViewReportKey = reportKey;

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->m_currentVideo.aid);
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["bvid"] = m_controller->m_currentVideo.bvid;
  params["played_time"] = "1";

  QPointer<BiliController> self(m_controller);
  m_controller->m_network->get(
      "/player/heartbeat", params,
      [self](const QJsonObject &) {
        if (!self)
          return;
      },
      [self, reportKey](int, const QString &) {
        if (!self)
          return;
        if (self->m_lastRecentViewReportKey == reportKey) {
          self->m_lastRecentViewReportKey.clear();
        }
      });
}

void BiliVideoModule::fetchVideoDetail(const QString &bvid) {
  if (bvid.isEmpty()) {
    emit m_controller->toastMessage("视频 ID 为空");
    return;
  }
  if (m_controller->m_videoDetailLoadingBvid == bvid) {
    return;
  }

  bool sameVideoRefresh = (m_controller->m_currentVideo.bvid == bvid && !bvid.isEmpty());
  // 同视频刷新时保存当前选中的 cid，避免 refreshDetail 解析后被重置到第一页
  const qint64 prevCid = sameVideoRefresh ? m_controller->m_currentVideo.cid : 0;

  // 仅在切换到新视频时重置收藏/投币/点赞/字幕状态，避免同视频刷新闪动
  if (!sameVideoRefresh) {
    if (m_controller->m_isFavorited) {
      m_controller->m_isFavorited = false;
      emit m_controller->favoriteStatusChanged();
    }
    if (m_controller->m_isCoined) {
      m_controller->m_isCoined = false;
      emit m_controller->coinStatusChanged();
    }
    if (m_controller->m_isLiked) {
      m_controller->m_isLiked = false;
      emit m_controller->likeStatusChanged();
    }
    if (m_controller->m_isWatchLater) {
      m_controller->m_isWatchLater = false;
      emit m_controller->watchLaterStatusChanged();
    }

    // 切换视频时重置字幕选择与列表
    if (m_controller->m_selectedSubtitleId != 0 || !m_controller->m_selectedSubtitleLabel.isEmpty()) {
      m_controller->m_selectedSubtitleId = 0;
      m_controller->m_selectedSubtitleLabel.clear();
      emit m_controller->selectedSubtitleChanged();
    }
    if (!m_controller->m_subtitleItems.isEmpty()) {
      m_controller->m_subtitleItems = QJsonArray();
      emit m_controller->subtitleListChanged();
    }
  }

  // BV号格式校验
  if (!bvid.startsWith("BV") || bvid.length() < 10) {
    emit m_controller->toastMessage("无效的视频 ID");
    return;
  }

  if (!sameVideoRefresh && m_controller->m_relatedVideoModel) {
    m_controller->m_relatedVideoModel->clear();
    m_controller->m_relatedVideoModel->setHasMore(false);
    m_controller->m_relatedVideoModel->setLoading(false);
    m_controller->m_relatedVideoModel->setErrorMessage("");
    m_controller->m_relatedVideoBvid.clear();
    m_controller->m_relatedVideoLoadingBvid.clear();
  }

  m_controller->setIsLoading(true);
  m_controller->m_videoDetailLoadingBvid = bvid;

  QMap<QString, QString> params;
  params["bvid"] = bvid;

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
      "/video/info", params,
      [self, sameVideoRefresh, prevCid](const QJsonObject &data) {
        if (!self)
          return;

        self->m_videoDetailLoadingBvid.clear();
        self->m_currentVideo = VideoListModel::parseVideoItem(data);
        self->m_currentVideo.aid = data.value("aid").toVariant().toLongLong();

        self->m_videoSeasonId = 0;
        self->m_videoSeasonTitle.clear();
        self->m_videoSeasonCover.clear();
        self->m_videoSeasonMid = 0;
        self->m_videoSeasonTotal = 0;
        QJsonObject ugcSeason = data.value("ugc_season").toObject();
        qint64 seasonId = ugcSeason.value("id").toVariant().toLongLong();
        if (seasonId > 0) {
          self->m_videoSeasonId = seasonId;
          self->m_videoSeasonTitle = ugcSeason.value("title").toString();
          self->m_videoSeasonCover = ugcSeason.value("cover").toString();
          if (self->m_videoSeasonCover.isEmpty()) {
            self->m_videoSeasonCover = self->m_currentVideo.pic;
          }
          self->m_videoSeasonMid = ugcSeason.value("mid").toVariant().toLongLong();
          if (self->m_videoSeasonMid <= 0) {
            self->m_videoSeasonMid = self->m_currentVideo.ownerMid;
          }
          bool totalKnown = false;
          int total = BiliJson::intValue(ugcSeason.value("ep_count"), &totalKnown);
          if (!totalKnown || total <= 0) {
            total = 0;
            QJsonArray sections = ugcSeason.value("sections").toArray();
            for (const QJsonValue &sectionValue : sections) {
              total += sectionValue.toObject().value("episodes").toArray().size();
            }
          }
          self->m_videoSeasonTotal = qMax(0, total);
        }

        QJsonArray pages = data.value("pages").toArray();

        // 解析分P列表（先更新模型，后决定 cid）
        if (pages.count() > 1) { // 只有多于1P时才显示列表
          QVector<VideoPartItem> parts;
          parts.reserve(pages.size());
          for (const QJsonValue &v : pages) {
            if (v.isObject()) {
              parts.append(VideoPartListModel::parseVideoPartItem(v.toObject()));
            }
          }
          self->m_videoPartModel->setItems(parts);
        } else {
          self->m_videoPartModel->clear();
        }

        // 决定当前 cid：
        // - 同视频刷新：优先保留 prevCid（若它存在于 pages 中）
        // - 否则：若解析不到 cid，则退回到第一页 cid
        if (!pages.isEmpty()) {
          if (sameVideoRefresh && prevCid > 0) {
            bool found = false;
            for (const QJsonValue &v : pages) {
              QJsonObject p = v.toObject();
              qint64 cid = p.value("cid").toVariant().toLongLong();
              if (cid > 0 && cid == prevCid) {
                found = true;
                break;
              }
            }
            if (found) {
              self->m_currentVideo.cid = prevCid;
            }
          }

          if (self->m_currentVideo.cid == 0) {
            QJsonObject firstPage = pages.first().toObject();
            self->m_currentVideo.cid = firstPage.value("cid").toVariant().toLongLong();
          }
        }

        emit self->videoDetailChanged();
        self->setIsLoading(false);
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->m_videoDetailLoadingBvid.clear();
        self->setIsLoading(false);
        self->m_videoPartModel->clear();
        emit self->toastMessage(QString("获取视频信息失败：%1").arg(msg));
      });
}

