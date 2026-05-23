#pragma once

#include <QObject>
#include <QtGlobal>
#include <QString>

class BiliController;

class BiliVideoModule : public QObject {
  Q_OBJECT
public:
  explicit BiliVideoModule(BiliController *controller);
  Q_INVOKABLE void reportCurrentVideoAsRecentViewIfNeeded();
  void refreshCurrentPlaybackProgress();
  Q_INVOKABLE void fetchVideoDetail(const QString &bvid);
  Q_INVOKABLE QObject *videoPartModel();

private:
  BiliController *m_controller;
};
