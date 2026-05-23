#pragma once

#include <QObject>
#include <QtGlobal>
#include <QString>

class BiliController;

class BiliUpModule : public QObject {
  Q_OBJECT
public:
  explicit BiliUpModule(BiliController *controller);
  Q_INVOKABLE void fetchUpInfo(qint64 mid);
  Q_INVOKABLE void fetchUpVideos(qint64 mid, int page = 1, int pageSize = 20);
  Q_INVOKABLE void fetchMoreUpVideos();
  Q_INVOKABLE void fetchUpSeasons(qint64 mid);
  Q_INVOKABLE void selectUpSeason(qint64 seasonId, const QString &name = QString(), bool isSeries = false, int total = 0);
  Q_INVOKABLE void toggleUpFollow();
  Q_INVOKABLE void playVideoPart(int index);
  Q_INVOKABLE void restartGoServer();
  Q_INVOKABLE void downloadVideoToDisk(int quality);
  Q_INVOKABLE QObject *upVideoModel();
  Q_INVOKABLE QObject *upSeasonModel();

private:
  BiliController *m_controller;
};
