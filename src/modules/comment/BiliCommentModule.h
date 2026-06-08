#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

class BiliController;

class BiliCommentModule : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool replyHasMore READ replyHasMore NOTIFY replyHasMoreChanged)
  Q_PROPERTY(bool commentsReady READ commentsReady NOTIFY commentsReadyChanged)
public:
  explicit BiliCommentModule(BiliController *controller);
  bool replyHasMore() const { return m_commentReplyHasMore; }
  bool commentsReady() const;
  Q_INVOKABLE void fetchComments(int page = 1, bool silent = false);
  Q_INVOKABLE void fetchCommentReplies(qint64 rootRpid);
  Q_INVOKABLE void fetchMoreCommentReplies();
  Q_INVOKABLE void fetchMoreComments();
  Q_INVOKABLE QObject *commentModel();
  Q_INVOKABLE QObject *commentReplyModel();
  void resetReplyState();

signals:
  void replyHasMoreChanged();
  void commentsReadyChanged();

private:
  BiliController *m_controller;
  int m_commentPage = 1;
  QString m_commentBvid;
  QString m_commentNextOffset;
  bool m_commentFirstPageLoaded = false;
  bool m_commentHasMore = true;
  int m_commentReplyPage = 1;
  bool m_commentReplyHasMore = false;
  qint64 m_currentCommentRootRpid = 0;
};
