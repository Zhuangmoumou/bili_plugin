#pragma once

#include <QtGlobal>
#include <QString>

class BiliController;

class BiliViewerModule {
public:
  explicit BiliViewerModule(BiliController *controller);
  void prepareImageForViewer(const QString &url);

private:
  BiliController *m_controller;
};
