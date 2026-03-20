#include "BiliModels.h"
#include <QDateTime>
#include <QDebug>
#include <cmath>

// ============ VideoListModel ============

VideoListModel::VideoListModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loading(false)
    , m_hasMore(true)
{
}

int VideoListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant VideoListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const VideoItem &item = m_items[index.row()];

    switch (role) {
    case BvidRole:      return item.bvid;
    case TitleRole:     return item.title;
    case PicRole:       return item.pic;
    case OwnerNameRole: return item.ownerName;
    case OwnerFaceRole: return item.ownerFace;
    case OwnerMidRole:  return item.ownerMid;
    case ViewsRole:     return formatCount(item.views);
    case DanmakuRole:   return formatCount(item.danmaku);
    case LikesRole:     return formatCount(item.likes);
    case DurationRole:  return item.duration;
    case CidRole:       return item.cid;
    case DescRole:      return item.desc;
    case RcmdReasonRole: return item.rcmdReason;
    case DurationTextRole: return formatDuration(item.duration);
    default: return QVariant();
    }
}

QHash<int, QByteArray> VideoListModel::roleNames() const
{
    return {
        {BvidRole, "bvid"},
        {TitleRole, "title"},
        {PicRole, "pic"},
        {OwnerNameRole, "ownerName"},
        {OwnerFaceRole, "ownerFace"},
        {OwnerMidRole, "ownerMid"},
        {ViewsRole, "views"},
        {DanmakuRole, "danmakuCount"},
        {LikesRole, "likes"},
        {DurationRole, "duration"},
        {CidRole, "cid"},
        {DescRole, "desc"},
        {RcmdReasonRole, "rcmdReason"},
        {DurationTextRole, "durationText"}
    };
}

int VideoListModel::count() const { return m_items.count(); }
bool VideoListModel::loading() const { return m_loading; }
bool VideoListModel::hasMore() const { return m_hasMore; }
QString VideoListModel::errorMessage() const { return m_errorMessage; }

void VideoListModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_errorMessage.clear();
    endResetModel();
    emit countChanged();
    emit errorMessageChanged();
}

void VideoListModel::appendItems(const QVector<VideoItem> &items)
{
    if (items.isEmpty()) return;

    beginInsertRows(QModelIndex(), m_items.count(),
                    m_items.count() + items.count() - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

void VideoListModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

void VideoListModel::setHasMore(bool hasMore)
{
    if (m_hasMore != hasMore) {
        m_hasMore = hasMore;
        emit hasMoreChanged();
    }
}

void VideoListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}

QString VideoListModel::formatDuration(int seconds)
{
    if (seconds <= 0) return "00:00";
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;
    if (h > 0) {
        return QString("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QString("%1:%2")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

QString VideoListModel::formatCount(qint64 count)
{
    if (count < 0) return "0";
    if (count < 10000) return QString::number(count);
    double wan = count / 10000.0;
    if (wan < 10000) {
        return QString("%1 万").arg(wan, 0, 'f', 1);
    }
    double yi = count / 100000000.0;
    return QString("%1 亿").arg(yi, 0, 'f', 1);
}

VideoItem VideoListModel::parseVideoItem(const QJsonObject &obj)
{
    VideoItem item;
    item.bvid = obj.value("bvid").toString();
    item.title = obj.value("title").toString();
    item.pic = obj.value("pic").toString();
    item.desc = obj.value("desc").toString();
    item.duration = obj.value("duration").toInt();
    item.cid = obj.value("cid").toVariant().toLongLong();
    item.pubdate = obj.value("pubdate").toVariant().toLongLong();

    QJsonObject owner = obj.value("owner").toObject();
    item.ownerName = owner.value("name").toString();
    item.ownerFace = owner.value("face").toString();
    item.ownerMid = owner.value("mid").toVariant().toLongLong();

    QJsonObject stat = obj.value("stat").toObject();
    item.views = stat.value("view").toVariant().toLongLong();
    item.danmaku = stat.value("danmaku").toVariant().toLongLong();
    item.likes = stat.value("like").toVariant().toLongLong();
    item.coins = stat.value("coin").toVariant().toLongLong();
    item.favorites = stat.value("favorite").toVariant().toLongLong();
    item.replies = stat.value("reply").toVariant().toLongLong();

    QJsonObject rcmd = obj.value("rcmd_reason").toObject();
    item.rcmdReason = rcmd.value("content").toString();

    return item;
}

// ============ CommentListModel ============

CommentListModel::CommentListModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loading(false)
    , m_totalCount(0)
{
}

int CommentListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant CommentListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const CommentItem &item = m_items[index.row()];

    switch (role) {
    case RpidRole:     return item.rpid;
    case UserNameRole: return item.userName;
    case AvatarRole:   return item.avatar;
    case LevelRole:    return item.level;
    case ContentRole:  return item.content;
    case LikesRole:    return item.likes;
    case RcountRole:   return item.rcount;
    case CtimeRole:    return item.ctime;
    case CtimeTextRole: return formatTime(item.ctime);
    case IsVipRole:    return item.isVip;
    default: return QVariant();
    }
}

QHash<int, QByteArray> CommentListModel::roleNames() const
{
    return {
        {RpidRole, "rpid"},
        {UserNameRole, "userName"},
        {AvatarRole, "avatar"},
        {LevelRole, "level"},
        {ContentRole, "content"},
        {LikesRole, "likes"},
        {RcountRole, "rcount"},
        {CtimeRole, "ctime"},
        {CtimeTextRole, "ctimeText"},
        {IsVipRole, "isVip"}
    };
}

int CommentListModel::count() const { return m_items.count(); }
bool CommentListModel::loading() const { return m_loading; }
int CommentListModel::totalCount() const { return m_totalCount; }
QString CommentListModel::errorMessage() const { return m_errorMessage; }

void CommentListModel::clear()
{
    beginResetModel();
    m_items.clear();
    m_errorMessage.clear();
    endResetModel();
    emit countChanged();
    emit errorMessageChanged();
}

void CommentListModel::appendItems(const QVector<CommentItem> &items)
{
    if (items.isEmpty()) return;
    beginInsertRows(QModelIndex(), m_items.count(),
                    m_items.count() + items.count() - 1);
    m_items.append(items);
    endInsertRows();
    emit countChanged();
}

void CommentListModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

void CommentListModel::setTotalCount(int total)
{
    if (m_totalCount != total) {
        m_totalCount = total;
        emit totalCountChanged();
    }
}

void CommentListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}

CommentItem CommentListModel::parseCommentItem(const QJsonObject &obj)
{
    CommentItem item;
    item.rpid = obj.value("rpid").toVariant().toLongLong();
    item.ctime = obj.value("ctime").toVariant().toLongLong();
    item.likes = obj.value("like").toVariant().toLongLong();
    item.rcount = obj.value("rcount").toVariant().toLongLong();

    QJsonObject member = obj.value("member").toObject();
    item.userName = member.value("uname").toString();
    item.avatar = member.value("avatar").toString();

    QJsonObject levelInfo = member.value("level_info").toObject();
    item.level = levelInfo.value("current_level").toInt();

    QJsonObject vip = member.value("vip").toObject();
    item.isVip = (vip.value("vipStatus").toInt() == 1);

    QJsonObject content = obj.value("content").toObject();
    item.content = content.value("message").toString();

    return item;
}

QString CommentListModel::formatTime(qint64 timestamp)
{
    QDateTime dt = QDateTime::fromSecsSinceEpoch(timestamp);
    QDateTime now = QDateTime::currentDateTime();
    qint64 diff = now.toSecsSinceEpoch() - timestamp;

    if (diff < 60) return "刚刚";
    if (diff < 3600) return QString("%1 分钟前").arg(diff / 60);
    if (diff < 86400) return QString("%1 小时前").arg(diff / 3600);
    if (diff < 2592000) return QString("%1 天前").arg(diff / 86400);

    return dt.toString("yyyy-MM-dd");
}

// ============ HotSearchModel ============

HotSearchModel::HotSearchModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_loading(false)
{
}

int HotSearchModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant HotSearchModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const HotSearchItem &item = m_items[index.row()];

    switch (role) {
    case KeywordRole: return item.keyword;
    case IconRole:    return item.icon;
    case PositionRole: return item.position;
    default: return QVariant();
    }
}

QHash<int, QByteArray> HotSearchModel::roleNames() const
{
    return {
        {KeywordRole, "keyword"},
        {IconRole, "icon"},
        {PositionRole, "position"}
    };
}

int HotSearchModel::count() const { return m_items.count(); }
bool HotSearchModel::loading() const { return m_loading; }

void HotSearchModel::clear()
{
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void HotSearchModel::setItems(const QVector<HotSearchItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void HotSearchModel::setLoading(bool loading)
{
    if (m_loading != loading) {
        m_loading = loading;
        emit loadingChanged();
    }
}

// ============ SearchResultModel ============

SearchResultModel::SearchResultModel(QObject *parent)
    : VideoListModel(parent)
{
}

QString SearchResultModel::keyword() const { return m_keyword; }

void SearchResultModel::setKeyword(const QString &keyword)
{
    if (m_keyword != keyword) {
        m_keyword = keyword;
        emit keywordChanged();
    }
}

void SearchResultModel::clear()
{
    VideoListModel::clear(); // 调用基类的 clear
    setKeyword("");          // 清空关键词
}

// ============ VideoPartListModel ============

VideoPartListModel::VideoPartListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int VideoPartListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_items.count();
}

QVariant VideoPartListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.count())
        return QVariant();

    const VideoPartItem &item = m_items[index.row()];

    switch (role) {
    case CidRole:      return item.cid;
    case PageRole:     return item.page;
    case PartRole:     return item.part;
    case DurationRole: return item.duration;
    case DurationTextRole: return VideoListModel::formatDuration(item.duration);
    default: return QVariant();
    }
}

QHash<int, QByteArray> VideoPartListModel::roleNames() const
{
    return {
        {CidRole, "cid"},
        {PageRole, "page"},
        {PartRole, "part"},
        {DurationRole, "duration"},
        {DurationTextRole, "durationText"}
    };
}

int VideoPartListModel::count() const { return m_items.count(); }

void VideoPartListModel::clear()
{
    if (m_items.isEmpty()) return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void VideoPartListModel::setItems(const QVector<VideoPartItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

VideoPartItem VideoPartListModel::parseVideoPartItem(const QJsonObject &obj)
{
    VideoPartItem item;
    item.cid = obj.value("cid").toVariant().toLongLong();
    item.page = obj.value("page").toInt();
    item.part = obj.value("part").toString();
    item.duration = obj.value("duration").toInt();
    return item;
}
