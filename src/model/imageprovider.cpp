/*
 * Copyright © 2015-2016 Antti Lamminsalo
 *
 * This file is part of Orion.
 *
 * Orion is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with Orion.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <QString>
#include <QUrl>
#include <QtGlobal>
#include <QDebug>
#include <QNetworkRequest>
#include "imageprovider.h"

ImageProvider::ImageProvider(const QString imageProviderName, const QString urlFormat, const QDir cacheDir, const QString extension) : 
    _imageProviderName(imageProviderName), _urlFormat(urlFormat), _cacheDir(cacheDir), _extension(extension) {

    activeDownloadCount = 0;
}

ImageProvider::~ImageProvider() {
    qDeleteAll(_imageTable);
}

bool ImageProvider::makeAvailable(QString key) {
    /* Make emote available by downloading it or loading it from cache if not already loaded.
    * Returns true if caller should wait for a downloadComplete event before using the emote */
    if (currentlyDownloading.contains(key)) {
        // download of this emote in progress
        return true;
    }
    else if (download(key)) {
        // if this emote isn't already downloading, it's safe to load the cache file or download if not in the cache
        currentlyDownloading.insert(key);
        activeDownloadCount += 1;
        return true;
    }
    else {
        // we already had the emote locally and don't need to wait for it to download
        return false;
    }
}

bool ImageProvider::download(QString key) {
    if (_imageTable.contains(key)) {
        qDebug() << "already in the table";
        return false;
    }

    QUrl url = _urlFormat.arg(key);
    _cacheDir.mkpath(".");

    QString filename = _cacheDir.absoluteFilePath(key + _extension);

    if (_cacheDir.exists(key + _extension)) {
        //qDebug() << "local file already exists";
        loadImageFile(key, filename);
        return false;
    }
    qDebug() << "downloading";

    QNetworkRequest request(url);
    QNetworkReply* _reply = nullptr;
    _reply = _manager.get(request);

    DownloadHandler * dh = new DownloadHandler(filename, key);

    connect(_reply, &QNetworkReply::readyRead,
        dh, &DownloadHandler::dataAvailable);
    connect(_reply, static_cast<void (QNetworkReply::*)(QNetworkReply::NetworkError)>(&QNetworkReply::error),
        dh, &DownloadHandler::error);
    connect(_reply, &QNetworkReply::finished,
        dh, &DownloadHandler::replyFinished);
    connect(dh, &DownloadHandler::downloadComplete,
        this, &ImageProvider::individualDownloadComplete);

    return true;
}

bool ImageProvider::bulkDownload(QList<QString> keys) {
    bool waitForDownloadComplete = false;
    for (auto key : keys) {
        if (makeAvailable(key)) {
            waitForDownloadComplete = true;
        }
    }
    return waitForDownloadComplete;
}


void ImageProvider::individualDownloadComplete(QString filename, bool hadError) {
    DownloadHandler * dh = qobject_cast<DownloadHandler*>(sender());
    const QString emoteKey = dh->getKey();
    delete dh;

    if (hadError) {
        // delete partial download if any
        QFile(filename).remove();
    }
    else {
        loadImageFile(emoteKey, filename);
    }

    if (activeDownloadCount > 0) {
        activeDownloadCount--;
        qDebug() << activeDownloadCount << "active downloads remaining";
    }

    currentlyDownloading.remove(emoteKey);

    if (activeDownloadCount == 0) {
        emit downloadComplete();
    }
}

QHash<QString, QImage*> ImageProvider::imageTable() {
    return _imageTable;
}

void ImageProvider::loadImageFile(QString emoteKey, QString filename) {
    QImage* emoteImg = new QImage();
    //qDebug() << "loading" << filename;
    emoteImg->load(filename);
    _imageTable.insert(emoteKey, emoteImg);
}

QQmlImageProviderBase * ImageProvider::getQMLImageProvider() {
    return new CachedImageProvider(_imageTable);
}

bool ImageProvider::downloadsInProgress() {
    return activeDownloadCount > 0;
}

// DownloadHandler

DownloadHandler::DownloadHandler(QString filename, QString key) : filename(filename), key(key), hadError(false) {
    _file.setFileName(filename);
    _file.open(QFile::WriteOnly);
    qDebug() << "starting download of" << filename;
}

void DownloadHandler::dataAvailable() {
    QNetworkReply* _reply = qobject_cast<QNetworkReply*>(sender());
    auto buffer = _reply->readAll();
    _file.write(buffer.data(), buffer.size());
}

void DownloadHandler::error(QNetworkReply::NetworkError code) {
    hadError = true;
    QNetworkReply* _reply = qobject_cast<QNetworkReply*>(sender());
    qDebug() << "Network error downloading" << filename << ":" << _reply->errorString();
}

void DownloadHandler::replyFinished() {
    QNetworkReply* _reply = qobject_cast<QNetworkReply*>(sender());
    if (_reply) {
        _reply->deleteLater();
        _file.close();
        //qDebug() << _file.fileName();
        //might need something for windows for the forwardslash..
        qDebug() << "download of" << _file.fileName() << "complete";

        emit downloadComplete(_file.fileName(), hadError);
    }
}


// CachedImageProvider
CachedImageProvider::CachedImageProvider(QHash<QString, QImage*> & imageTable) : QQuickImageProvider(QQuickImageProvider::Image), imageTable(imageTable) {

}

QImage CachedImageProvider::requestImage(const QString &id, QSize * size, const QSize & requestedSize) {
    //qDebug() << "Requested id" << id << "from image provider";
    QImage * entry = NULL;
    auto result = imageTable.find(id);
    if (result != imageTable.end()) {
        entry = *result;
    }
    if (entry) {
        if (size) {
            *size = entry->size();
        }
        return *entry;
    }
    return QImage();
}
