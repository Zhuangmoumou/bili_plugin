#pragma once

#include <QtGlobal>

class BiliController;

class BiliLoginModule {
public:
  explicit BiliLoginModule(BiliController *controller);

  void generateQrcode();
  void pollQrcode();
  void startSmsLogin();
  void stopSmsLogin();
  void pollSmsLogin();
  void checkLoginStatus();
  void refreshLoginInfo();
  void fetchUserInfo(qint64 mid);
  void logout();

private:
  BiliController *m_controller;
};
