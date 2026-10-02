#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <array>

// A "P2MT" motion block: one animation for a whole skeleton.
//
//   0x00 "P2MT", u32 0x7000, u32 bone count, u32 0
//   0x10 bone records, 0x50 bytes each:
//        u32 key offset (from the block start, 0 = not animated), u32 key count,
//        u32 channels, u32 0, float matrix[16] (the bone's rest pose for this motion)
// Channels: bit 0 rotation, bit 2 translation, bit 3 scale.
// Key: u32 frames until the next key (0 on the last), 12 bytes, then for each
//      channel present, in this order: quaternion w, x, y, z; translation[4]; scale[4].
// The key durations of every bone add up to the motion's length in frames.
// The rotation replaces the rest rotation; a translation key is added to the
// rest translation.
struct P2Motion {
    struct Key {
        int frame = 0;                         // start frame of the key
        std::array<float, 4> rotation{{1, 0, 0, 0}};  // w, x, y, z as stored
        std::array<float, 3> translation{{0, 0, 0}};
        std::array<float, 3> scale{{1, 1, 1}};
    };
    struct Bone {
        quint32 channels = 0;
        std::array<float, 16> matrix{};
        QVector<Key> keys;
    };

    qint64 offset = 0;
    qint64 size = 0;
    int length = 0;   // frames
    QVector<Bone> bones;

    static bool parse(const QByteArray &data, qint64 offset, P2Motion &motion, QString *error = nullptr);
};
