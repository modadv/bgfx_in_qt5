// terrain_asset_service.h
#pragma once
#include "terrain_hash_utils.h"

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QFileInfo>

#include <memory>

class TerrainAssetService : public QObject {
    Q_OBJECT
public:
    explicit TerrainAssetService(QObject* parent=nullptr);

    // 触发下载；返回 key（sha1(url)）
    Q_INVOKABLE QString request(const QUrl& url);

    // 如已缓存（我们自己的资产目录），立即返回路径；否则空串
    Q_INVOKABLE QString cachedPath(const QUrl& url) const;

    Q_INVOKABLE QString assetsRoot() const { return m_assetsRoot; }
    Q_INVOKABLE QUrl pathToUrl(const QString& path) const {
        return QUrl::fromLocalFile(path);
    }
signals:
    void ready(QString key, QString filePath);      // 成功：本地路径
    void failed(QString key, int code, QString msg);// 失败

private:
    QString keyFor(const QUrl& url) const { return sha1Hex(url.toString()); }
    static QString guessExtFromContentType(const QString& ct);
    static QString guessExtFromUrl(const QUrl& url);

    QString finalPathFor(const QString& key, const QString& ext) const {
        return m_assetsRoot + "/data/" + key + (ext.isEmpty() ? "" : "." + ext);
    }

private:
    std::unique_ptr<QNetworkAccessManager> _nam;
    std::unique_ptr<QNetworkDiskCache> _disk; // 仅用于省流量；稳定路径由我们自己管
    QString m_assetsRoot;
};

