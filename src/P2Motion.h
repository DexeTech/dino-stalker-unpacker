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
// The rest matrix is this motion's own: it holds the pose of every bone and channel the
// motion does not key, and its translation is the absolute base of the translation keys.
// It differs from the model's rest pose, and from motion to motion (a motion that
// continues another starts where that one ended).
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

        // The pose the rest matrix gives: its rotation and scale, translation 0.
        Key rest() const;
        // Key k (negative: from the end) with the channels the bone does not key taken
        // from rest(); rest() when the bone has no keys.
        Key full(int k) const;
    };

    qint64 offset = 0;
    qint64 size = 0;
    int length = 0;   // frames
    QVector<Bone> bones;

    static bool parse(const QByteArray &data, qint64 offset, P2Motion &motion, QString *error = nullptr);
};
