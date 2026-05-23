#pragma once

#include <QObject>
#include <QtGlobal>

class BiliController;

class BiliFeedModule : public QObject {
  Q_OBJECT
public:
  explicit BiliFeedModule(BiliController *controller);
  Q_INVOKABLE void fetchPopular(int page = 1, int pageSize = 10);
  Q_INVOKABLE void fetchMorePopular();
  Q_INVOKABLE void fetchRanking(int rid = 0);
  Q_INVOKABLE void fetchHotSearch();
  Q_INVOKABLE QObject *popularModel();
  Q_INVOKABLE QObject *rankingModel();
  Q_INVOKABLE QObject *hotSearchModel();

private:
  BiliController *m_controller;
  int m_popularPage = 1;
};
