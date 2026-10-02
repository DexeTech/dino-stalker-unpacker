#include "Iso9660.h"
#include "Binary.h"

namespace {
const qint64 SECTOR = 2048;
const qint64 VOLUME_DESCRIPTOR = 16 * SECTOR;
}

bool Iso9660::isIsoImage(const QString &fileName) {
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly) || f.size() < VOLUME_DESCRIPTOR + SECTOR) {
        return false;
    }
    f.seek(VOLUME_DESCRIPTOR);
    const QByteArray pvd = f.read(8);
    return pvd.size() == 8 && pvd[0] == 1 && pvd.mid(1, 5) == "CD001";
}

bool Iso9660::open(const QString &fileName) {
    entries.clear();
    file.setFileName(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    file.seek(VOLUME_DESCRIPTOR);
    const QByteArray pvd = file.read(SECTOR);
    Reader r(pvd);
    if (pvd.size() != SECTOR || r.u8(0) != 1 || !r.matches(1, "CD001")) {
        error = "No ISO9660 file system found; this is not a disc image.";
        return false;
    }
    volume = r.name(40, 32).trimmed();
    // The root directory record is at 156: extent at +2, size at +10.
    if (!readDirectory(r.u32(156 + 2), r.u32(156 + 10), QString(), 0)) {
        return false;
    }
    return true;
}

bool Iso9660::readDirectory(quint32 sector, quint32 size, const QString &path, int depth) {
    if (depth > 16 || size > 16 * 1024 * 1024) {
        error = "Directory structure is damaged.";
        return false;
    }
    file.seek(qint64(sector) * SECTOR);
    const QByteArray dir = file.read(size);
    if (dir.size() != qint64(size)) {
        error = "Disc image is truncated.";
        return false;
    }
    Reader r(dir);
    qint64 pos = 0;
    while (pos < dir.size()) {
        const int length = r.u8(pos);
        if (length == 0) {
            // Records do not cross sectors; skip the padding to the next one.
            pos = (pos / SECTOR + 1) * SECTOR;
            continue;
        }
        if (length < 34 || !r.has(pos, length)) {
            error = "Directory record is damaged.";
            return false;
        }
        const quint32 extent = r.u32(pos + 2);
        const quint32 extentSize = r.u32(pos + 10);
        const int flags = r.u8(pos + 25);
        const int nameLength = r.u8(pos + 32);
        const QByteArray rawName = dir.mid(pos + 33, nameLength);
        pos += length;
        if (rawName == QByteArray(1, '\0') || rawName == QByteArray(1, '\1')) {
            continue;
        }
        QString name = QString::fromLatin1(rawName);
        name = name.section(';', 0, 0);
        const QString full = path.isEmpty() ? name : path + "/" + name;
        if (flags & 2) {
            if (!readDirectory(extent, extentSize, full, depth + 1)) {
                return false;
            }
        } else {
            entries.append({full, extent, extentSize});
        }
    }
    return true;
}

QByteArray Iso9660::read(const Entry &entry) {
    file.seek(qint64(entry.sector) * SECTOR);
    return file.read(entry.size);
}
