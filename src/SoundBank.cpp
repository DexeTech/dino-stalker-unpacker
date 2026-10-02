#include "SoundBank.h"
#include "Audio.h"
#include "Binary.h"

#include <QDir>
#include <QFile>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace {

const qint64 ALIGN = 0x800;
const int DEFAULT_RATE = 22050;

qint64 alignUp(qint64 v) { return (v + ALIGN - 1) / ALIGN * ALIGN; }

struct Region {
    qint64 start = 0, end = 0;
};

bool saveRaw(const QString &fileName, const QByteArray &data, qint64 offset, qint64 size) {
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data.constData() + offset, size);
    return true;
}

bool isScei(const Reader &r, qint64 o, const char *second) {
    return r.matches(o, "IECSsreV") && r.matches(o + 0x10, second);
}

bool overlaps(const QVector<Region> &used, qint64 start, qint64 end) {
    for (const Region &u : used) {
        if (start < u.end && u.start < end) return true;
    }
    return false;
}

} // namespace

namespace SoundBank {

bool isSoundBin(const QByteArray &data) {
    Reader r(data);
    return r.matches(0, "pBAV") || isScei(r, 0, "IECSuqeS") || isScei(r, 0, "IECSdaeH");
}

bool extract(const QByteArray &data, const QString &outputDir, QStringList &messages, QString *error) {
    Reader r(data);
    QDir().mkpath(outputDir);

    struct Vab { qint64 offset, headerSize; QVector<qint64> sizes; QVector<int> rates; };
    struct Hd { qint64 offset, headerSize, bodySize; };
    struct Sq { qint64 offset, size; };
    QVector<Vab> vabs;
    QVector<Hd> hds;
    QVector<Sq> sqs;
    QVector<Region> used;

    for (qint64 o = 0; o + 0x40 <= data.size(); o += ALIGN) {
        if (r.matches(o, "pBAV")) {
            const int programs = r.u16(o + 18), vags = r.u16(o + 22);
            const qint64 header = 0x20 + 0x800 + qint64(programs) * 0x200 + 0x200;
            if (programs > 128 || vags > 255 || !r.has(o, header)) continue;
            Vab vab{o, header, {}, {}};
            const qint64 table = o + 0x20 + 0x800 + qint64(programs) * 0x200;
            for (int v = 1; v <= vags; ++v) vab.sizes.append(qint64(r.u16(table + v * 2)) * 8);
            vab.rates.fill(0, vags);
            for (int p = 0; p < programs; ++p) {
                for (int t = 0; t < 16; ++t) {
                    const qint64 tone = o + 0x820 + qint64(p) * 0x200 + t * 0x20;
                    const int vag = r.s16(tone + 0x16);
                    const int centre = r.u8(tone + 4), fine = r.u8(tone + 5);
                    if (vag >= 1 && vag <= vags && vab.rates[vag - 1] == 0 && centre >= 24 && centre <= 108) {
                        vab.rates[vag - 1] = int(std::lround(44100.0 / std::pow(2.0, (centre + fine / 128.0 - 60) / 12.0)));
                    }
                }
            }
            vabs.append(vab);
            used.append({o, o + header});
        } else if (isScei(r, o, "IECSuqeS")) {
            const qint64 midi = o + 0x30;
            if (!r.matches(midi, "IECSidiM")) continue;
            const qint64 size = 0x30 + r.u32(midi + 8);
            if (!r.has(o, size)) continue;
            sqs.append({o, size});
            used.append({o, o + size});
        } else if (isScei(r, o, "IECSdaeH")) {
            const qint64 header = r.u32(o + 0x10 + 12), body = r.u32(o + 0x10 + 16);
            if (!r.has(o, header)) continue;
            hds.append({o, header, body});
            used.append({o, o + header});
        }
    }

    // Place bodies: VAB samples follow their header; a BD follows its HD at the next
    // 0x800 boundary unless something else is there, in which case it is the data
    // just before the HD.
    struct Body { qint64 offset = -1, size = 0; };
    QVector<Body> vabBodies(vabs.size()), hdBodies(hds.size());
    for (int i = 0; i < vabs.size(); ++i) {
        qint64 size = 0;
        for (qint64 s : vabs[i].sizes) size += s;
        const qint64 start = vabs[i].offset + vabs[i].headerSize;
        if (r.has(start, size) && !overlaps(used, start, start + size)) {
            vabBodies[i] = {start, size};
            used.append({start, start + size});
        }
    }
    for (int i = 0; i < hds.size(); ++i) {
        const qint64 after = alignUp(hds[i].offset + hds[i].headerSize);
        const qint64 before = hds[i].offset - alignUp(hds[i].bodySize);
        if (r.has(after, hds[i].bodySize) && !overlaps(used, after, after + hds[i].bodySize)) {
            hdBodies[i] = {after, hds[i].bodySize};
        } else if (before >= 0 && !overlaps(used, before, before + hds[i].bodySize)) {
            hdBodies[i] = {before, hds[i].bodySize};
        }
        if (hdBodies[i].offset >= 0) used.append({hdBodies[i].offset, hdBodies[i].offset + hdBodies[i].size});
    }

    int written = 0;
    for (int i = 0; i < vabs.size(); ++i) {
        const Vab &vab = vabs[i];
        const QString dir = QString("%1/vab_%2").arg(outputDir).arg(i, 2, 10, QChar('0'));
        QDir().mkpath(dir);
        const Body &body = vabBodies[i];
        if (body.offset < 0) {
            saveRaw(dir + "/bank.vh", data, vab.offset, vab.headerSize);
            messages << QString("vab_%1: header only at 0x%2 (%3 samples); its sample data is not next to it")
                            .arg(i, 2, 10, QChar('0')).arg(vab.offset, 0, 16).arg(vab.sizes.size());
            continue;
        }
        saveRaw(dir + "/bank.vab", data, vab.offset, vab.headerSize + body.size);
        qint64 p = body.offset;
        for (int v = 0; v < vab.sizes.size(); ++v) {
            const QVector<qint16> pcm = Audio::decodeAdpcm(data, p, vab.sizes[v], false);
            const int rate = vab.rates[v] ? vab.rates[v] : DEFAULT_RATE;
            Audio::writeWav(QString("%1/%2.wav").arg(dir).arg(v, 3, 10, QChar('0')), pcm, rate, 1);
            p += vab.sizes[v];
            ++written;
        }
    }
    for (int i = 0; i < sqs.size(); ++i) {
        saveRaw(QString("%1/sequence_%2.sq").arg(outputDir).arg(i, 2, 10, QChar('0')), data, sqs[i].offset, sqs[i].size);
    }
    for (int i = 0; i < hds.size(); ++i) {
        const Hd &hd = hds[i];
        const QString dir = QString("%1/instruments_%2").arg(outputDir).arg(i, 2, 10, QChar('0'));
        QDir().mkpath(dir);
        saveRaw(dir + "/bank.hd", data, hd.offset, hd.headerSize);
        const Body &body = hdBodies[i];
        if (body.offset < 0) {
            messages << QString("instruments_%1: sample body not found").arg(i, 2, 10, QChar('0'));
            continue;
        }
        saveRaw(dir + "/bank.bd", data, body.offset, body.size);
        // Head chunk at +0x10: magic, size, header size, body size, then the offsets
        // of the Prog, Sset, Smpl and Vagi chunks.
        const qint64 vagi = hd.offset + r.u32(hd.offset + 0x10 + 0x20);
        if (!r.matches(vagi, "IECSigaV")) continue;
        const int count = int(r.u32(vagi + 12)) + 1;
        QVector<qint64> offsets;
        QVector<int> rates;
        for (int v = 0; v < count && v < 1024; ++v) {
            const qint64 entry = vagi + r.u32(vagi + 16 + v * 4);
            offsets << r.u32(entry);
            rates << r.u16(entry + 4);
        }
        for (int v = 0; v < offsets.size(); ++v) {
            const qint64 next = v + 1 < offsets.size() ? offsets[v + 1] : body.size;
            const qint64 start = body.offset + offsets[v];
            const QVector<qint16> pcm = Audio::decodeAdpcm(data, start, std::max<qint64>(0, next - offsets[v]), true);
            Audio::writeWav(QString("%1/%2.wav").arg(dir).arg(v, 3, 10, QChar('0')), pcm, rates[v] ? rates[v] : DEFAULT_RATE, 1);
            ++written;
        }
    }

    // Whatever is left: raw ADPCM played by offset. Split at end flags.
    std::sort(used.begin(), used.end(), [](const Region &a, const Region &b) { return a.start < b.start; });
    qint64 pos = 0;
    int raw = 0;
    QVector<Region> gaps;
    for (const Region &u : used) {
        if (u.start > pos) gaps.append({pos, u.start});
        pos = std::max(pos, u.end);
    }
    if (pos < data.size()) gaps.append({pos, data.size()});
    for (const Region &gap : gaps) {
        // Small gaps are sector padding left after a bank.
        if (gap.end - gap.start < 0x8000) continue;
        qint64 p = gap.start;
        // Skip padding up to the first non-zero 16-byte line.
        int index = 0;
        QString dir;
        while (p + 16 <= gap.end) {
            bool zero = true;
            for (int k = 0; k < 16 && zero; ++k) zero = data[int(p + k)] == 0;
            if (zero) {
                p += 16;
                continue;
            }
            qint64 consumed = 0;
            // Include the silent block before the sample, as VAGs do.
            const QVector<qint16> pcm = Audio::decodeAdpcm(data, p, gap.end - p, true, &consumed);
            if (consumed <= 0) break;
            if (pcm.size() >= 28 * 4) {
                if (dir.isEmpty()) {
                    dir = QString("%1/unindexed_%2_%3").arg(outputDir).arg(raw++, 2, 10, QChar('0')).arg(gap.start, 7, 16, QChar('0'));
                    QDir().mkpath(dir);
                }
                Audio::writeWav(QString("%1/%2_%3.wav").arg(dir).arg(index++, 3, 10, QChar('0')).arg(p, 7, 16, QChar('0')),
                                pcm, DEFAULT_RATE, 1);
                ++written;
            }
            p += consumed;
        }
    }
    messages << QString("%1 VAB banks, %2 sequences, %3 instrument banks, %4 unindexed sample areas, %5 WAV files")
                    .arg(vabs.size()).arg(sqs.size()).arg(hds.size()).arg(raw).arg(written);
    Q_UNUSED(error);
    return true;
}

} // namespace SoundBank
