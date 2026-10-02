#include "Lzss.h"
#include "Binary.h"

namespace Lzss {

bool looksCompressed(const QByteArray &data) {
    if (data.size() < 16) {
        return false;
    }
    Reader r(data);
    const quint32 size = r.u32(0);
    // The packs are 2-3x smaller than their contents, and every one begins with a
    // P2 block whose magic is stored as literals right after the first flag byte.
    return size > quint32(data.size()) && size < 0x10000000 && r.matches(5, "P2");
}

QByteArray decompress(const QByteArray &data, QString *error) {
    Reader r(data);
    const quint32 size = r.u32(0);
    if (data.size() < 4 || size > 0x10000000) {
        if (error) *error = "Not a compressed pack.";
        return QByteArray();
    }
    QByteArray out(int(size), '\0');
    char *dst = out.data();
    const char *src = data.constData();
    qint64 in = 4;
    qint64 pos = 0;
    while (pos < qint64(size)) {
        if (in >= data.size()) {
            if (error) *error = "Compressed data ends early.";
            return QByteArray();
        }
        const quint8 flags = quint8(src[in++]);
        for (int bit = 0; bit < 8 && pos < qint64(size); ++bit) {
            if (flags & (1 << bit)) {
                if (in >= data.size()) {
                    if (error) *error = "Compressed data ends early.";
                    return QByteArray();
                }
                dst[pos++] = src[in++];
            } else {
                if (in + 2 > data.size()) {
                    if (error) *error = "Compressed data ends early.";
                    return QByteArray();
                }
                const quint8 b0 = quint8(src[in]);
                const quint8 b1 = quint8(src[in + 1]);
                in += 2;
                const int length = (b0 & 0x1F) + 3;
                const qint64 distance = (b0 >> 5) | (qint64(b1) << 3);
                for (int i = 0; i < length && pos < qint64(size); ++i, ++pos) {
                    dst[pos] = (distance <= pos && distance > 0) ? dst[pos - distance] : 0;
                }
            }
        }
    }
    return out;
}

} // namespace Lzss
