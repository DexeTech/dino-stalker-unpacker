#include "Audio.h"

#include <QFile>
#include <algorithm>

namespace Audio {

QVector<qint16> decodeAdpcm(const QByteArray &data, qint64 offset, qint64 size, bool stopAtEnd,
                            qint64 *consumed) {
    static const int f0[5] = {0, 60, 115, 98, 122};
    static const int f1[5] = {0, 0, -52, -55, -60};
    QVector<qint16> out;
    const qint64 end = std::min<qint64>(data.size(), offset + size);
    const uchar *src = reinterpret_cast<const uchar *>(data.constData());
    int s1 = 0, s2 = 0;
    qint64 p = offset;
    for (; p + 16 <= end; p += 16) {
        const int shift = src[p] & 0x0F;
        const int filter = std::min(src[p] >> 4, 4);
        const int flags = src[p + 1];
        for (int i = 0; i < 28; ++i) {
            const int nibble = (src[p + 2 + i / 2] >> ((i & 1) * 4)) & 0x0F;
            int sample = qint16(nibble << 12) >> std::min(shift, 12);
            sample += (s1 * f0[filter] + s2 * f1[filter] + 32) >> 6;
            sample = std::clamp(sample, -32768, 32767);
            out.append(qint16(sample));
            s2 = s1;
            s1 = sample;
        }
        if (stopAtEnd && (flags & 1)) {
            p += 16;
            break;
        }
    }
    if (consumed) *consumed = p - offset;
    return out;
}

bool writeWav(const QString &fileName, const QVector<qint16> &samples, int sampleRate, int channels,
              QString *error) {
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char *>(&v), 2); };
    const quint32 bytes = quint32(samples.size() * 2);
    f.write("RIFF");
    u32(36 + bytes);
    f.write("WAVEfmt ");
    u32(16);
    u16(1);
    u16(quint16(channels));
    u32(quint32(sampleRate));
    u32(quint32(sampleRate * channels * 2));
    u16(quint16(channels * 2));
    u16(16);
    f.write("data");
    u32(bytes);
    f.write(reinterpret_cast<const char *>(samples.constData()), bytes);
    return true;
}

} // namespace Audio
