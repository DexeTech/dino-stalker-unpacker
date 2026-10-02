#pragma once

#include <QFile>
#include <QString>
#include <QVector>

// Reads files from a PlayStation 2 disc image (ISO9660, 2048-byte sectors).
class Iso9660 {
public:
    struct Entry {
        QString path;   // e.g. "DATA/ENEMYDT/MD/TREXMD.BIN"
        quint32 sector;
        quint32 size;
    };

    bool open(const QString &fileName);
    QString errorString() const { return error; }
    QString volumeName() const { return volume; }
    const QVector<Entry> &files() const { return entries; }
    QByteArray read(const Entry &entry);

    // True if the file starts like an ISO9660 image.
    static bool isIsoImage(const QString &fileName);

private:
    bool readDirectory(quint32 sector, quint32 size, const QString &path, int depth);

    QFile file;
    QString error;
    QString volume;
    QVector<Entry> entries;
};
