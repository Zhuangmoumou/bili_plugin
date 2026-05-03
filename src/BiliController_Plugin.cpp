#include "BiliController.h"
#include "BiliImageProvider.h"
#include "BiliModels.h"
#include "BiliNetwork.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QThread>
#include <QtQml>
#include <signal.h>
#include <iostream>

static QQmlEngine *s_engine = nullptr;

extern "C" {

// 全局变量
static BiliImageProvider *s_imageProvider = nullptr;
static QMutex s_providerMutex;
static QProcess *s_apiServerProcess = nullptr;
static const QString API_SERVER_PATH =
    "/userdisk/PenMods/plugins/bili_plugin/";
static const QString API_SERVER_EXEC = "server"; // Go 编译的可执行文件名

// 查找并结束旧的 API 服务进程
static void killExistingApiServer() {
  std::cout << "BiliPlugin: Checking for existing API server processes..."
            << std::endl;

  QString execPath = API_SERVER_PATH + "/" + API_SERVER_EXEC;

  // 使用 pgrep 查找进程
  QProcess pgrepProcess;
  pgrepProcess.start("pgrep", QStringList() << "-f" << execPath);
  pgrepProcess.waitForFinished(2000);

  if (pgrepProcess.exitCode() == 0) {
    QString output =
        QString::fromLocal8Bit(pgrepProcess.readAllStandardOutput()).trimmed();
    QStringList pids = output.split('\n', Qt::SkipEmptyParts);

    for (const QString &pid : pids) {
      bool ok;
      int pidNum = pid.toInt(&ok);
      if (ok && pidNum > 0) {
        std::cout << "BiliPlugin: Killing existing process PID: " << pidNum
                  << std::endl;
        kill(pidNum, SIGTERM);
      }
    }

    // 等待进程退出
    QThread::msleep(500);

    // 强制杀死仍在运行的进程
    for (const QString &pid : pids) {
      bool ok;
      int pidNum = pid.toInt(&ok);
      if (ok && pidNum > 0) {
        if (kill(pidNum, 0) == 0) {
          std::cout << "BiliPlugin: Force killing PID: " << pidNum << std::endl;
          kill(pidNum, SIGKILL);
        }
      }
    }
  }

  std::cout << "BiliPlugin: Existing API server cleanup done" << std::endl;
}

// 启动 API 服务器
static bool startApiServerImpl() {
  std::cout << "BiliPlugin: Starting API server..." << std::endl;

  // 先结束旧进程
  killExistingApiServer();

  // 等待端口释放
  QThread::msleep(300);

  // 检查可执行文件是否存在
  QString execPath = API_SERVER_PATH + "/" + API_SERVER_EXEC;
  if (!QFile::exists(execPath)) {
    std::cerr << "BiliPlugin: API server executable not found: "
              << execPath.toStdString() << std::endl;
    return false;
  }

  // 检查执行权限
  QFile serverFile(execPath);
  if (!(serverFile.permissions() & QFile::ExeUser)) {
    std::cout << "BiliPlugin: Setting executable permission..." << std::endl;
    serverFile.setPermissions(serverFile.permissions() | QFile::ExeUser |
                              QFile::ExeGroup | QFile::ExeOther);
  }

  // 创建新进程
  s_apiServerProcess = new QProcess();
  s_apiServerProcess->setWorkingDirectory(API_SERVER_PATH);

  // 连接信号用于调试
  QObject::connect(
      s_apiServerProcess, &QProcess::readyReadStandardOutput, []() {
        if (s_apiServerProcess) {
          std::cout << "API Server: "
                    << s_apiServerProcess->readAllStandardOutput().constData();
        }
      });

  QObject::connect(s_apiServerProcess, &QProcess::readyReadStandardError, []() {
    if (s_apiServerProcess) {
      std::cerr << "API Server Error: "
                << s_apiServerProcess->readAllStandardError().constData();
    }
  });

  QObject::connect(
      s_apiServerProcess,
      QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      [](int exitCode, QProcess::ExitStatus exitStatus) {
        std::cout << "BiliPlugin: API server exited with code " << exitCode
                  << ", status: "
                  << (exitStatus == QProcess::NormalExit ? "normal" : "crashed")
                  << std::endl;
      });

  // 直接启动 Go 可执行文件
  s_apiServerProcess->start(execPath, QStringList());

  // 等待启动
  if (!s_apiServerProcess->waitForStarted(5000)) {
    std::cerr << "BiliPlugin: Failed to start API server: "
              << s_apiServerProcess->errorString().toStdString() << std::endl;
    delete s_apiServerProcess;
    s_apiServerProcess = nullptr;
    return false;
  }

  // 等待服务器初始化
  QThread::msleep(1000);

  // 检查进程是否还在运行
  if (s_apiServerProcess->state() != QProcess::Running) {
    std::cerr << "BiliPlugin: API server failed to stay running" << std::endl;
    delete s_apiServerProcess;
    s_apiServerProcess = nullptr;
    return false;
  }

  std::cout << "BiliPlugin: API server started successfully, PID: "
            << s_apiServerProcess->processId() << std::endl;
  return true;
}

// 停止 API 服务器
static void stopApiServerImpl() {
  std::cout << "BiliPlugin: Stopping API server..." << std::endl;

  if (s_apiServerProcess) {
    // 优雅关闭
    s_apiServerProcess->terminate();

    // 等待退出
    if (!s_apiServerProcess->waitForFinished(3000)) {
      std::cout << "BiliPlugin: API server not responding, force killing..."
                << std::endl;
      s_apiServerProcess->kill();
      s_apiServerProcess->waitForFinished(2000);
    }

    delete s_apiServerProcess;
    s_apiServerProcess = nullptr;
  }

  // 确保清理所有残留进程
  killExistingApiServer();

  std::cout << "BiliPlugin: API server stopped" << std::endl;
}

} // extern "C"

// 供控制器调用的 API Server 控制函数
bool bili_startApiServer() {
  return startApiServerImpl();
}

void bili_stopApiServer() {
  stopApiServerImpl();
}

extern "C" {

void init_plugin() {
  std::cout << "BiliPlugin: Initializing..." << std::endl;

  qmlRegisterType<BiliController>("BiliPlugin", 1, 0, "BiliController");
  qmlRegisterType<VideoListModel>("BiliPlugin", 1, 0, "VideoListModel");
  qmlRegisterType<CommentListModel>("BiliPlugin", 1, 0, "CommentListModel");
  qmlRegisterType<HotSearchModel>("BiliPlugin", 1, 0, "HotSearchModel");
  qmlRegisterType<SearchResultModel>("BiliPlugin", 1, 0, "SearchResultModel");
  qmlRegisterType<VideoPartListModel>("BiliPlugin", 1, 0, "VideoPartListModel");
  qmlRegisterType<FavoriteFolderModel>("BiliPlugin", 1, 0, "FavoriteFolderModel");
  qmlRegisterType<UpSeasonListModel>("BiliPlugin", 1, 0, "UpSeasonListModel");

  // 启动 API 服务器
  if (!bili_startApiServer()) {
    std::cerr << "BiliPlugin: Warning - API server failed to start"
              << std::endl;
  }

  std::cout << "BiliPlugin: Registered successfully!" << std::endl;
}

void attach_engine(QQmlEngine *engine) {
  QMutexLocker locker(&s_providerMutex);

  s_engine = engine;

  if (s_engine) {
    QString pluginPath = "/userdisk/PenMods/plugins/bili_plugin/qml";
    engine->addImportPath(pluginPath);

    QString playerPath = "/userdisk/PenMods/plugins/bili_plugin/";
    engine->addImportPath(playerPath);

    BiliNetwork *network = BiliNetwork::instance();
    s_imageProvider = new BiliImageProvider(network);

    s_engine->addImageProvider("bili", s_imageProvider);

    std::cout << "BiliPlugin: Engine attached, ImageProvider registered"
              << std::endl;
  }
}

void destroy_plugin() {
  std::cout << "BiliPlugin: Destroying..." << std::endl;

  // 先停止 API 服务器
  bili_stopApiServer();

  {
    QMutexLocker locker(&s_providerMutex);
    s_imageProvider = nullptr;
  }

  // 取消所有网络请求
  BiliNetwork *network = BiliNetwork::instance();
  if (network) {
    network->cancelAllRequests();
  }

  QThread::msleep(200);

  s_engine = nullptr;

  std::cout << "BiliPlugin: Destroyed successfully" << std::endl;
}

} // extern "C"
