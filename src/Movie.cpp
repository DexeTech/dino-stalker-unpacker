#include "Movie.h"
#include "Audio.h"
#include "Binary.h"

#include <QFile>
#include <QVector>

namespace Movie {

bool isMovie(const QByteArray &head) {
    Reader r(head);
    return head.size() >= 14 && r.u32(0) == 0xBA010000u && (r.u8(4) & 0xC0) == 0x40;
}

bool split(const QByteArray &d, const QString &videoFile, const QString &audioFile, QString *summary,
           QString *error) {
    const uchar *p = reinterpret_cast<const uchar *>(d.constData());
    QFile video(videoFile);
    if (!video.open(QIODevice::WriteOnly)) {
        if (error) *error = video.errorString();
        return false;
    }
    QByteArray audio;
    qint64 o = 0;
    while (o + 4 <= d.size()) {
        if (p[o] != 0 || p[o + 1] != 0 || p[o + 2] != 1) {
            if (error) *error = QString("Stream is damaged at 0x%1.").arg(o, 0, 16);
            return false;
        }
        const int id = p[o + 3];
        if (id == 0xBA) {
            if (o + 14 > d.size()) break;
            o += 14 + (p[o + 13] & 7);
            continue;
        }
        if (id == 0xB9) {
            o += 4;
            continue;
        }
        if (o + 6 > d.size()) break;
        const int length = (p[o + 4] << 8) | p[o + 5];
        if (o + 6 + length > d.size()) break;
        if (id == 0xE0 || id == 0xBD) {
            const int headerLength = p[o + 8];
            const qint64 start = o + 9 + headerLength;
            const qint64 end = o + 6 + length;
            if (id == 0xE0) {
                video.write(d.constData() + start, end - start);
            } else if (end - start > 4 && p[start] == 0xFF) {
                audio.append(d.constData() + start + 4, int(end - start - 4));
            }
        }
        o += 6 + length;
    }
    video.close();

    QString audioNote = "no audio";
    Reader a(audio);
    if (a.matches(0, "SShd")) {
        const qint64 headerSize = 8 + a.u32(4);
        const int format = int(a.u32(8)), rate = int(a.u32(12)), channels = int(a.u32(16));
        const qint64 interleave = a.u32(20);
        const qint64 body = headerSize + 8;
        if (a.matches(headerSize, "SSbd") && format == 1 && channels >= 1 && channels <= 8 && interleave > 0
            && interleave % 2 == 0) {
            const qint64 size = std::min<qint64>(a.u32(headerSize + 4), audio.size() - body);
            const qint64 frame = interleave * channels;
            const qint64 frames = size / frame;
            QVector<qint16> pcm;
            pcm.reserve(int(frames * frame / 2));
            for (qint64 f = 0; f < frames; ++f) {
                const qint64 base = body + f * frame;
                for (qint64 s = 0; s < interleave / 2; ++s)
                    for (int c = 0; c < channels; ++c) pcm.append(a.s16(base + c * interleave + s * 2));
            }
            Audio::writeWav(audioFile, pcm, rate, channels);
            audioNote = QString("%1 Hz, %2 channels").arg(rate).arg(channels);
        } else {
            audioNote = QString("audio format %1 not supported").arg(format);
        }
    }
    if (summary) *summary = audioNote;
    return true;
}

} // namespace Movie
