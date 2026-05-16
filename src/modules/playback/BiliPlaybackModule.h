#pragma once

#include <QtGlobal>
#include <QString>
#include <QStringList>

class BiliController;

class BiliPlaybackModule {
public:
  explicit BiliPlaybackModule(BiliController *controller);
  void fetchPlayUrl(int quality = 64);
  void fetchAcceptQualities(int quality = 64);
  void downloadAndPlay(int quality = 64);
  void cancelDownload();
  void cleanupTempVideo();
  bool externalPlayerRunning() const;
  void launchExternalPlayer(const QString &path);
  void launchExternalPlayerWithAudio(const QString &videoPath, const QString &audioPath);
  void launchExternalPlayerWithAudioUrl(const QString &videoUrl, const QString &audioUrl);
  void launchExternalPlayerWithAudioUrlAndSubtitle(const QString &videoUrl, const QString &audioUrl, const QString &subtitlePath);
  void fetchSubtitleList();
  void selectSubtitle(qint64 subtitleId, const QString &label);
  void clearSelectedSubtitle();
  void setSubtitleFontSize(int value);
  void setSubtitleMarginV(int value);
  void setSubtitleSpacing(double value);
  void setSubtitleWeight(int value);
  void setSubtitleColorPreset(const QString &value);
  void setSubtitleOutlineEnabled(bool enabled);
  void setSubtitleOutlineWidth(int value);
  void setSubtitleBackgroundEnabled(bool enabled);
  void launchExternalPlayerCurrentSelection();

private:
  bool isExternalPlayerRunning() const;
  QString externalPlayerTitle() const;
  int resumeStartSeconds() const;
  void appendResumeStartArg(QStringList &args) const;
  bool startExternalPlayer(const QStringList &args);
  BiliController *m_controller;
};
