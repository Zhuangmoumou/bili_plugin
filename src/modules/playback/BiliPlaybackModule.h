#pragma once

#include <QObject>
#include <QtGlobal>
#include <QString>
#include <QStringList>

class BiliController;

class BiliPlaybackModule : public QObject {
  Q_OBJECT
public:
  explicit BiliPlaybackModule(BiliController *controller);
  Q_INVOKABLE void fetchPlayUrl(int quality = 64);
  Q_INVOKABLE void fetchAcceptQualities(int quality = 64);
  Q_INVOKABLE void downloadAndPlay(int quality = 64);
  Q_INVOKABLE void cancelDownload();
  Q_INVOKABLE void cleanupTempVideo();
  Q_INVOKABLE bool externalPlayerRunning() const;
  Q_INVOKABLE void launchExternalPlayer(const QString &path);
  Q_INVOKABLE void launchExternalPlayerWithAudio(const QString &videoPath, const QString &audioPath);
  Q_INVOKABLE void launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl);
  Q_INVOKABLE void launchExternalPlayerWithAudioUrlAndSubtitle(const QString &videoUrl, const QString &audioUrl, const QString &subtitlePath);
  Q_INVOKABLE void fetchSubtitleList();
  Q_INVOKABLE void selectSubtitle(qint64 subtitleId, const QString &label);
  Q_INVOKABLE void clearSelectedSubtitle();
  Q_INVOKABLE void setSubtitleFontSize(int value);
  Q_INVOKABLE void setSubtitleMarginV(int value);
  Q_INVOKABLE void setSubtitleSpacing(double value);
  Q_INVOKABLE void setSubtitleWeight(int value);
  Q_INVOKABLE void setSubtitleColorPreset(const QString &value);
  Q_INVOKABLE void setSubtitleOutlineEnabled(bool enabled);
  Q_INVOKABLE void setSubtitleOutlineWidth(int value);
  Q_INVOKABLE void setSubtitleBackgroundEnabled(bool enabled);
  Q_INVOKABLE void setSubtitleBackgroundOpacity(double value);
  Q_INVOKABLE void launchExternalPlayerCurrentSelection();

private:
  bool isExternalPlayerRunning() const;
  QString externalPlayerTitle() const;
  int resumeStartSeconds() const;
  void appendResumeStartArg(QStringList &args) const;
  bool startExternalPlayer(const QStringList &args);
  BiliController *m_controller;
};
