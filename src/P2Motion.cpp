#include "P2Motion.h"
#include "Binary.h"

#include <algorithm>
#include <cmath>

P2Motion::Key P2Motion::Bone::rest() const {
    // The row-vector matrix applies to row vectors, as the stored quaternions do. Read as
    // a column-vector matrix (r[i][j] = matrix[4j + i]) its rotation is the conjugate of
    // the stored one.
    Key key;
    float r[3][3];
    for (int j = 0; j < 3; ++j) {
        const float len = std::sqrt(matrix[4 * j] * matrix[4 * j] + matrix[4 * j + 1] * matrix[4 * j + 1]
                                    + matrix[4 * j + 2] * matrix[4 * j + 2]);
        key.scale[j] = len;
        for (int i = 0; i < 3; ++i) r[i][j] = len > 1e-12f ? matrix[4 * j + i] / len : (i == j ? 1.0f : 0.0f);
    }
    float x, y, z, w;
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2;
        x = (r[2][1] - r[1][2]) / s, y = (r[0][2] - r[2][0]) / s, z = (r[1][0] - r[0][1]) / s, w = 0.25f * s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
        x = 0.25f * s, y = (r[0][1] + r[1][0]) / s, z = (r[0][2] + r[2][0]) / s, w = (r[2][1] - r[1][2]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
        x = (r[0][1] + r[1][0]) / s, y = 0.25f * s, z = (r[1][2] + r[2][1]) / s, w = (r[0][2] - r[2][0]) / s;
    } else {
        const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
        x = (r[0][2] + r[2][0]) / s, y = (r[1][2] + r[2][1]) / s, z = 0.25f * s, w = (r[1][0] - r[0][1]) / s;
    }
    const float l = std::sqrt(x * x + y * y + z * z + w * w);
    key.rotation = {w / l, -x / l, -y / l, -z / l};
    return key;
}

P2Motion::Key P2Motion::Bone::full(int k) const {
    Key key = rest();
    if (keys.isEmpty()) return key;
    const Key &stored = keys[k < 0 ? keys.size() + k : k];
    key.frame = stored.frame;
    if (channels & 1) key.rotation = stored.rotation;
    if (channels & 4) key.translation = stored.translation;
    if (channels & 8) key.scale = stored.scale;
    return key;
}

bool P2Motion::parse(const QByteArray &data, qint64 offset, P2Motion &motion, QString *error) {
    Reader r(data);
    auto fail = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!r.matches(offset, "P2MT")) {
        return fail("Not a P2MT block.");
    }
    const quint32 count = r.u32(offset + 8);
    if (count == 0 || count > 512 || !r.has(offset + 0x10, qint64(count) * 0x50)) {
        return fail("Bone table is out of range.");
    }
    motion.offset = offset;
    motion.length = 0;
    motion.bones.clear();
    qint64 end = offset + 0x10 + qint64(count) * 0x50;
    for (quint32 b = 0; b < count; ++b) {
        const qint64 rec = offset + 0x10 + qint64(b) * 0x50;
        Bone bone;
        const quint32 keyOffset = r.u32(rec);
        const quint32 keys = r.u32(rec + 4);
        bone.channels = r.u32(rec + 8);
        for (int k = 0; k < 16; ++k) {
            bone.matrix[k] = r.f32(rec + 0x10 + k * 4);
        }
        if (keyOffset && keys) {
            const qint64 keySize = 0x10 + ((bone.channels & 1) ? 0x10 : 0) + ((bone.channels & 4) ? 0x10 : 0)
                                   + ((bone.channels & 8) ? 0x10 : 0);
            if (keys > 0x10000 || !r.has(offset + keyOffset, qint64(keys) * keySize)) {
                return fail(QString("Bone %1 keys are out of range.").arg(b));
            }
            int frame = 0;
            qint64 p = offset + keyOffset;
            for (quint32 k = 0; k < keys; ++k, p += keySize) {
                Key key;
                key.frame = frame;
                const quint32 duration = r.u32(p);
                qint64 q = p + 0x10;
                if (bone.channels & 1) {
                    for (int c = 0; c < 4; ++c) {
                        key.rotation[c] = r.f32(q + c * 4);
                    }
                    q += 0x10;
                }
                if (bone.channels & 4) {
                    key.translation = {r.f32(q), r.f32(q + 4), r.f32(q + 8)};
                    q += 0x10;
                }
                if (bone.channels & 8) {
                    key.scale = {r.f32(q), r.f32(q + 4), r.f32(q + 8)};
                }
                bone.keys.append(key);
                if (duration > 0x10000) {
                    return fail(QString("Bone %1 has an invalid key duration.").arg(b));
                }
                frame += int(duration);
            }
            motion.length = std::max(motion.length, frame);
            end = std::max(end, p);
        }
        motion.bones.append(bone);
    }
    if (!r.ok()) {
        return fail("Motion data is truncated.");
    }
    motion.size = end - offset;
    return true;
}
