#include "modules/playback/BiliPlaybackModule.h"
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

BiliPlaybackModule::BiliPlaybackModule(BiliController *controller)
    : m_controller(controller) {}

// ====== API: 播放地址 ======

void BiliPlaybackModule::fetchPlayUrl(int quality) {
  if (m_controller->m_currentVideo.bvid.isEmpty() || m_controller->m_currentVideo.cid == 0) {
    emit m_controller->toastMessage("视频信息不完整，无法播放");
    return;
  }

  const bool audioOnly = (quality == 0);
  const int requestedQuality = audioOnly ? 16 : qBound(16, quality, 127);
  const QString requestKey = QString("%1:%2:%3:%4")
                                 .arg(m_controller->m_currentVideo.bvid)
                                 .arg(m_controller->m_currentVideo.cid)
                                 .arg(quality)
                                 .arg(4048);
  if (m_controller->m_playUrlLoadingKey == requestKey) {
    return;
  }
  m_controller->setIsLoading(true);
  m_controller->m_playUrlLoadingKey = requestKey;
  m_controller->m_playUrl.clear();
  m_controller->m_dashVideoUrl.clear();
  m_controller->m_dashAudioUrl.clear();
  emit m_controller->playUrlChanged();

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->videoAid());
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["qn"] = QString::number(requestedQuality);
  params["bvid"] = m_controller->m_currentVideo.bvid;
  // fnval=1: 优先请求 MP4 格式，fnval=16: DASH 格式（音视频分离）
  // 优先使用 MP4 格式以获得更好的兼容性
  params["fnval"] = "4048";

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
      "/video/playurl", params,
      [self, requestedQuality, audioOnly](const QJsonObject &data) {
        if (!self)
          return;

        self->m_playUrlLoadingKey.clear();
        QString videoUrl;
        QString audioUrl;
        int finalQuality = audioOnly ? 0 : requestedQuality;
        int apiQuality = data.value("quality").toInt(0);
        if (!audioOnly && apiQuality > 0) {
          finalQuality = apiQuality;
        }

        self->updateAcceptQualities(data);

        if (audioOnly) {
          auto dash = self->pickDashUrls(data, requestedQuality);
          audioUrl = dash.audioUrl;
          if (audioUrl.isEmpty()) {
            emit self->toastMessage("未获取到音频播放地址");
            self->setIsLoading(false);
            return;
          }
          videoUrl = audioUrl;
        } else {
          videoUrl = self->pickMp4Url(data);
          if (videoUrl.isEmpty()) {
            auto dash = self->pickDashUrls(data, requestedQuality);
            videoUrl = dash.videoUrl;
            audioUrl = dash.audioUrl;
            finalQuality = dash.finalQuality;
          }
        }

        // 保存 DASH 直链，供外部播放器流式播放
        self->m_dashVideoUrl = audioOnly ? QString() : videoUrl;
        self->m_dashAudioUrl = audioUrl;

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到播放地址");
          self->setIsLoading(false);
          return;
        }

        // URL 安全验证
        QUrl parsedUrl(videoUrl);
        if (!parsedUrl.isValid() || (!parsedUrl.scheme().startsWith("http"))) {
          emit self->toastMessage("播放地址无效");
          self->setIsLoading(false);
          return;
        }

        self->m_playUrl = videoUrl;
        self->m_playQuality = finalQuality;

        if (!audioOnly && !audioUrl.isEmpty()) {
          QUrl parsedAudio(audioUrl);
          if (parsedAudio.isValid()) {
            qDebug() << "[BiliController] DASH: video + audio";
          }
        }

        emit self->playUrlChanged();
        self->setIsLoading(false);

        if (!self->m_playUrl.isEmpty()) {
          emit self->playbackReady(self->m_playUrl);
        }
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->m_playUrlLoadingKey.clear();
        self->m_playUrl.clear();
        self->m_dashVideoUrl.clear();
        self->m_dashAudioUrl.clear();
        emit self->playUrlChanged();
        self->setIsLoading(false);
        emit self->toastMessage(QString("获取播放地址失败：%1").arg(msg));
      });
}

// ====== 仅获取可用清晰度 ======

void BiliPlaybackModule::fetchAcceptQualities(int quality) {
  if (m_controller->m_currentVideo.bvid.isEmpty() || m_controller->m_currentVideo.cid == 0) {
    emit m_controller->toastMessage("视频信息不完整，无法获取清晰度");
    return;
  }

  quality = qBound(16, quality, 127);
  const QString requestKey = QString("%1:%2:%3:%4")
                                 .arg(m_controller->m_currentVideo.bvid)
                                 .arg(m_controller->m_currentVideo.cid)
                                 .arg(quality)
                                 .arg(4048);
  if (m_controller->m_acceptQualitiesLoadingKey == requestKey) {
    return;
  }
  m_controller->setIsLoading(true);
  m_controller->m_acceptQualitiesLoadingKey = requestKey;

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->videoAid());
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_controller->m_currentVideo.bvid;
  params["fnval"] = "4048";

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
      "/video/playurl", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;

        self->m_acceptQualitiesLoadingKey.clear();
        self->updateAcceptQualities(data);
        self->setIsLoading(false);
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->m_acceptQualitiesLoadingKey.clear();
        self->setIsLoading(false);
        emit self->toastMessage(QString("获取清晰度失败：%1").arg(msg));
      });
}

// ====== 下载并播放视频 ======

void BiliPlaybackModule::downloadAndPlay(int quality) {
  if (m_controller->m_currentVideo.bvid.isEmpty() || m_controller->m_currentVideo.cid == 0) {
    emit m_controller->toastMessage("视频信息不完整，无法下载");
    return;
  }

  if (m_controller->m_isDownloading) {
    emit m_controller->toastMessage("正在下载中，请稍候...");
    return;
  }

  quality = qBound(16, quality, 127);
  m_controller->setIsLoading(true);

  // 先清理旧的临时文件
  cleanupTempVideo();

  // 生成临时文件路径
  QString tempDir =
      QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  QString baseName = QString("bili_%1_%2")
                         .arg(m_controller->m_currentVideo.bvid)
                         .arg(QDateTime::currentMSecsSinceEpoch());
  m_controller->m_tempVideoPath = QDir(tempDir).filePath(baseName + ".m4s");
  m_controller->m_tempAudioPath = QDir(tempDir).filePath(baseName + "_audio.m4s");

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->videoAid());
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["qn"] = QString::number(quality);
  params["bvid"] = m_controller->m_currentVideo.bvid;
  params["fnval"] = "1"; // MP4 合并流

  QPointer<BiliController> self(m_controller);

  m_controller->m_network->get(
      "/video/playurl", params,
      [self, quality](const QJsonObject &data) {
        if (!self)
          return;

        QString videoUrl;
        QString audioUrl;
        int requestedQuality = quality;
        int finalQuality = requestedQuality;
        int apiQuality = data.value("quality").toInt(0);
        if (apiQuality > 0) {
          finalQuality = apiQuality;
        }

        self->updateAcceptQualities(data);

        auto dash = self->pickDashUrls(data, requestedQuality);
        videoUrl = dash.videoUrl;
        audioUrl = dash.audioUrl;
        finalQuality = dash.finalQuality;

        // 回退到 MP4（durl）
        if (videoUrl.isEmpty()) {
          videoUrl = self->pickMp4Url(data);
        }

        if (videoUrl.isEmpty()) {
          emit self->toastMessage("未获取到播放地址");
          self->setIsLoading(false);
          return;
        }

        self->startDownloadTask(videoUrl, audioUrl, self->m_tempVideoPath,
                                self->m_tempAudioPath, finalQuality, true);
      },
      [self](int code, const QString &msg) {
        Q_UNUSED(code)
        if (!self)
          return;

        self->setIsLoading(false);
        emit self->toastMessage(QString("获取播放地址失败：%1").arg(msg));
      });
}

void BiliPlaybackModule::cancelDownload() {
  if (!m_controller->m_isDownloading)
    return;

  // 调用新的、专门的取消下载方法，避免死锁
  m_controller->m_network->cancelVideoDownload();

  // 这里可以立即更新UI状态，因为网络层的abort()会异步触发错误回调
  // 错误回调会再次更新状态，但这里的即时更新能提供更好的用户反馈
  m_controller->m_isDownloading = false;
  m_controller->m_downloadStatus = "正在取消...";
  emit m_controller->downloadStateChanged();
  emit m_controller->toastMessage("正在取消下载...");
}

void BiliPlaybackModule::cleanupTempVideo() {
  if (!m_controller->m_tempVideoPath.isEmpty()) {
    QFile file(m_controller->m_tempVideoPath);
    if (file.exists()) {
      file.remove();
      std::cout << "[BiliController] Cleaned up temp video: "
                << m_controller->m_tempVideoPath.toStdString() << std::endl;
    }
    m_controller->m_tempVideoPath.clear();
  }
  if (!m_controller->m_tempAudioPath.isEmpty()) {
    QFile file(m_controller->m_tempAudioPath);
    if (file.exists()) {
      file.remove();
      std::cout << "[BiliController] Cleaned up temp audio: "
                << m_controller->m_tempAudioPath.toStdString() << std::endl;
    }
    m_controller->m_tempAudioPath.clear();
  }
  m_controller->m_playUrl.clear();
  m_controller->m_dashVideoUrl.clear();
  m_controller->m_dashAudioUrl.clear();
  emit m_controller->playUrlChanged();
}

bool BiliPlaybackModule::isExternalPlayerRunning() const {
  if (m_controller->m_externalPlayerProcess && m_controller->m_externalPlayerProcess->state() != QProcess::NotRunning) {
    return true;
  }

  QProcess pgrep;
  pgrep.start("pgrep", QStringList() << "-f" << "/userdisk/mpv/bin/mpv");
  if (!pgrep.waitForFinished(1500)) {
    return false;
  }

  return pgrep.exitCode() == 0 && !QString::fromLocal8Bit(pgrep.readAllStandardOutput()).trimmed().isEmpty();
}

bool BiliPlaybackModule::externalPlayerRunning() const {
  return isExternalPlayerRunning();
}

QString BiliPlaybackModule::externalPlayerTitle() const {
  QString title = m_controller->videoTitle().trimmed();
  if (title.isEmpty()) {
    title = m_controller->m_currentVideo.bvid.trimmed();
  }
  return title;
}

bool BiliPlaybackModule::startExternalPlayer(const QStringList &args) {
  const QString player = "/userdisk/VideoPlayer";
  if (!QFile::exists(player)) {
    emit m_controller->toastMessage("外部播放器不存在");
    return false;
  }

  if (isExternalPlayerRunning()) {
    emit m_controller->toastMessage("播放器已在运行，请先关闭当前窗口");
    return false;
  }

  auto *process = new QProcess(m_controller);
  process->setProgram(player);
  process->setArguments(args);

  QObject::connect(process, &QProcess::errorOccurred, m_controller,
          [this, process](QProcess::ProcessError error) {
            if (process != m_controller->m_externalPlayerProcess) {
              process->deleteLater();
              return;
            }

            QString detail = process->errorString();
            if (detail.isEmpty()) {
              detail = QString::number(static_cast<int>(error));
            }
            emit m_controller->toastMessage(QString("启动外部播放器失败：%1").arg(detail));
            m_controller->m_externalPlayerProcess = nullptr;
            process->deleteLater();
          });

  QObject::connect(process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), m_controller,
          [this, process](int, QProcess::ExitStatus) {
            if (process == m_controller->m_externalPlayerProcess) {
              m_controller->m_externalPlayerProcess = nullptr;
            }
            process->deleteLater();
          });

  process->start();
  if (!process->waitForStarted(3000)) {
    QString detail = process->errorString();
    const QString stderrText = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
    if (!stderrText.isEmpty()) {
      detail = detail.isEmpty() ? stderrText : detail + " | " + stderrText;
    }
    emit m_controller->toastMessage(QString("启动外部播放器失败：%1").arg(detail.isEmpty() ? QStringLiteral("未知错误") : detail));
    process->deleteLater();
    return false;
  }

  m_controller->m_externalPlayerProcess = process;
  return true;
}

void BiliPlaybackModule::launchExternalPlayer(const QString &path) {
  if (path.isEmpty()) {
    emit m_controller->toastMessage("播放路径为空");
    return;
  }

  QString filePath = path;
  if (filePath.startsWith("file://")) {
    filePath = filePath.mid(7);
  }

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle()) << filePath;
  if (filePath.startsWith("http")) {
    args << "--referrer=https://www.bilibili.com"
         << "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
         << ("--script-opts=bili-aid=" + QString::number(m_controller->videoAid())
             + ",bili-cid=" + QString::number(m_controller->m_currentVideo.cid)
             + ",bili-bvid=" + m_controller->m_currentVideo.bvid);
  }
  startExternalPlayer(args);
}

void BiliPlaybackModule::launchExternalPlayerWithAudio(const QString &videoPath, const QString &audioPath) {
  if (videoPath.isEmpty() || audioPath.isEmpty()) {
    emit m_controller->toastMessage("播放路径不完整");
    return;
  }

  QString v = videoPath;
  QString a = audioPath;
  if (v.startsWith("file://")) v = v.mid(7);
  if (a.startsWith("file://")) a = a.mid(7);

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle())
       << v << ("--audio-file=" + a);
  startExternalPlayer(args);
}

void BiliPlaybackModule::launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl) {
  if (videoUrl.isEmpty() || audioUrl.isEmpty()) {
    emit m_controller->toastMessage("播放地址不完整");
    return;
  }

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle())
       << videoUrl << ("--audio-file=" + audioUrl)
       << "--referrer=https://www.bilibili.com"
       << "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
       << ("--script-opts=bili-aid=" + QString::number(m_controller->videoAid())
           + ",bili-cid=" + QString::number(m_controller->m_currentVideo.cid)
           + ",bili-bvid=" + m_controller->m_currentVideo.bvid);

  startExternalPlayer(args);
}

void BiliPlaybackModule::launchExternalPlayerWithAudioUrlAndSubtitle(const QString &videoUrl, const QString &audioUrl, const QString &subtitlePath) {
  if (videoUrl.isEmpty() || audioUrl.isEmpty()) {
    emit m_controller->toastMessage("播放地址不完整");
    return;
  }

  QString sub = subtitlePath;
  if (sub.startsWith("file://")) {
    sub = sub.mid(7);
  }

  QStringList args;
  args << ("--force-media-title=" + externalPlayerTitle())
       << videoUrl << ("--audio-file=" + audioUrl);
  if (!sub.isEmpty()) {
    args << ("--sub-file=" + sub);
  }
  args << "--referrer=https://www.bilibili.com"
       << "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"
       << ("--script-opts=bili-aid=" + QString::number(m_controller->videoAid())
           + ",bili-cid=" + QString::number(m_controller->m_currentVideo.cid)
           + ",bili-bvid=" + m_controller->m_currentVideo.bvid);

  qDebug() << "[BiliController] launchExternalPlayerWithAudioUrlAndSubtitle"
           << "sub=" << sub;

  startExternalPlayer(args);
}

void BiliPlaybackModule::fetchSubtitleList() {
  if (m_controller->m_currentVideo.aid <= 0 || m_controller->m_currentVideo.cid <= 0) {
    emit m_controller->toastMessage("视频信息不完整，无法获取字幕");
    return;
  }

  QMap<QString, QString> params;
  params["aid"] = QString::number(m_controller->m_currentVideo.aid);
  params["cid"] = QString::number(m_controller->m_currentVideo.cid);
  params["bvid"] = m_controller->m_currentVideo.bvid;

  QPointer<BiliController> self(m_controller);
  m_controller->m_network->get(
      "/video/subtitle/list", params,
      [self](const QJsonObject &data) {
        if (!self)
          return;
        self->m_subtitleItems = data.value("subtitles").toArray();
        emit self->subtitleListChanged();
      },
      [self](int, const QString &msg) {
        if (!self)
          return;
        self->m_subtitleItems = QJsonArray();
        emit self->subtitleListChanged();
        emit self->toastMessage(QString("获取字幕列表失败：%1").arg(msg));
      });
}

void BiliPlaybackModule::selectSubtitle(qint64 subtitleId, const QString &label) {
  if (m_controller->m_selectedSubtitleId == subtitleId && m_controller->m_selectedSubtitleLabel == label) {
    return;
  }
  m_controller->m_selectedSubtitleId = subtitleId;
  m_controller->m_selectedSubtitleLabel = label;
  emit m_controller->selectedSubtitleChanged();
}

void BiliPlaybackModule::clearSelectedSubtitle() {
  if (m_controller->m_selectedSubtitleId == 0 && m_controller->m_selectedSubtitleLabel.isEmpty()) {
    return;
  }
  m_controller->m_selectedSubtitleId = 0;
  m_controller->m_selectedSubtitleLabel.clear();
  emit m_controller->selectedSubtitleChanged();
}

void BiliPlaybackModule::setSubtitleFontSize(int value) {
  value = qBound(6, value, 40);
  if (m_controller->m_subtitleFontSize == value) return;
  m_controller->m_subtitleFontSize = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleFontSize", m_controller->m_subtitleFontSize);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleMarginV(int value) {
  value = qBound(0, value, 30);
  if (m_controller->m_subtitleMarginV == value) return;
  m_controller->m_subtitleMarginV = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleMarginV", m_controller->m_subtitleMarginV);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleSpacing(double value) {
  if (value < 0) value = 0;
  if (value > 6.0) value = 6.0;
  if (qFuzzyCompare(m_controller->m_subtitleSpacing, value)) return;
  m_controller->m_subtitleSpacing = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleSpacing", m_controller->m_subtitleSpacing);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleWeight(int value) {
  value = qBound(100, value, 900);
  if (m_controller->m_subtitleWeight == value) return;
  m_controller->m_subtitleWeight = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleWeight", m_controller->m_subtitleWeight);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleColorPreset(const QString &value) {
  QString preset = value;
  if (preset != "white" && preset != "yellow" && preset != "cyan" && preset != "black") {
    preset = "white";
  }
  if (m_controller->m_subtitleColorPreset == preset) return;
  m_controller->m_subtitleColorPreset = preset;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleColorPreset", m_controller->m_subtitleColorPreset);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleOutlineEnabled(bool enabled) {
  if (m_controller->m_subtitleOutlineEnabled == enabled) return;
  m_controller->m_subtitleOutlineEnabled = enabled;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleOutlineEnabled", m_controller->m_subtitleOutlineEnabled);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::setSubtitleOutlineWidth(int value) {
  value = qBound(1, value, 6);
  if (m_controller->m_subtitleOutlineWidth == value) return;
  m_controller->m_subtitleOutlineWidth = value;
  QSettings settings("BiliPocket", "BiliPlugin");
  settings.setValue("subtitleOutlineWidth", m_controller->m_subtitleOutlineWidth);
  settings.sync();
  emit m_controller->subtitleStyleChanged();
}

void BiliPlaybackModule::launchExternalPlayerCurrentSelection() {
  if (m_controller->m_dashVideoUrl.isEmpty() || m_controller->m_dashAudioUrl.isEmpty()) {
    if (!m_controller->m_playUrl.isEmpty()) {
      launchExternalPlayer(m_controller->m_playUrl);
      return;
    }
    emit m_controller->toastMessage("播放地址尚未准备好");
    return;
  }

  if (m_controller->m_selectedSubtitleId <= 0) {
    launchExternalPlayerWithAudioUrl(m_controller->m_dashVideoUrl, m_controller->m_dashAudioUrl);
    return;
  }

  QUrl subtitleUrl(m_controller->m_network->apiBase() + "/video/subtitle/ass/file");
  QUrlQuery subtitleQuery;
  subtitleQuery.addQueryItem("aid", QString::number(m_controller->m_currentVideo.aid));
  subtitleQuery.addQueryItem("cid", QString::number(m_controller->m_currentVideo.cid));
  subtitleQuery.addQueryItem("bvid", m_controller->m_currentVideo.bvid);
  subtitleQuery.addQueryItem("sid", QString::number(m_controller->m_selectedSubtitleId));
  subtitleQuery.addQueryItem("font_size", QString::number(m_controller->m_subtitleFontSize));
  subtitleQuery.addQueryItem("margin_v", QString::number(m_controller->m_subtitleMarginV));
  subtitleQuery.addQueryItem("spacing", QString::number(m_controller->m_subtitleSpacing, 'f', 2));
  subtitleQuery.addQueryItem("weight", QString::number(m_controller->m_subtitleWeight));
  subtitleQuery.addQueryItem("color_preset", m_controller->m_subtitleColorPreset);
  subtitleQuery.addQueryItem("outline_enabled", m_controller->m_subtitleOutlineEnabled ? "1" : "0");
  subtitleQuery.addQueryItem("outline_width", QString::number(m_controller->m_subtitleOutlineWidth));
  subtitleUrl.setQuery(subtitleQuery);

  launchExternalPlayerWithAudioUrlAndSubtitle(
      m_controller->m_dashVideoUrl, m_controller->m_dashAudioUrl, subtitleUrl.toString());
}
