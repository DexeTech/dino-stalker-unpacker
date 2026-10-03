#include "MotionChains.h"

#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

constexpr float MaxAngle = 3.0f;          // degrees, any bone
constexpr float MaxDistance = 0.02f;      // of the skeleton's size, any bone
constexpr int Ambiguous = 3;              // a pose this many motions share is not followed

// A bone's rotation (as stored) and absolute translation at one end of a motion.
struct BonePose {
    std::array<float, 4> rotation;
    std::array<float, 3> position;
};
using Pose = QVector<BonePose>;

Pose poseAt(const P2Motion &motion, bool end) {
    Pose pose;
    for (const P2Motion::Bone &bone : motion.bones) {
        const P2Motion::Key key = bone.full(end ? -1 : 0);
        pose.append({key.rotation, {bone.matrix[12] + key.translation[0], bone.matrix[13] + key.translation[1],
                                    bone.matrix[14] + key.translation[2]}});
    }
    return pose;
}

bool samePose(const Pose &a, const Pose &b, float size) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        float dot = 0, dist = 0;
        for (int c = 0; c < 4; ++c) dot += a[i].rotation[c] * b[i].rotation[c];
        for (int c = 0; c < 3; ++c) dist += (a[i].position[c] - b[i].position[c]) * (a[i].position[c] - b[i].position[c]);
        const float angle = 2 * std::acos(std::min(1.0f, std::fabs(dot))) * 180.0f / 3.14159265f;
        if (angle >= MaxAngle || std::sqrt(dist) >= MaxDistance * size) return false;
    }
    return true;
}

int keyedBones(const P2Motion &motion) {
    return int(std::count_if(motion.bones.begin(), motion.bones.end(),
                             [](const P2Motion::Bone &b) { return (b.channels & 1) && !b.keys.isEmpty(); }));
}

} // namespace

QVector<QVector<int>> findMotionChains(const QVector<const P2Motion *> &motions) {
    QVector<QVector<int>> chains;
    if (motions.size() < 2) return chains;

    // Skeleton size: the longest bone offset.
    float size = 0;
    for (const P2Motion::Bone &bone : motions.first()->bones)
        size = std::max(size, std::sqrt(bone.matrix[12] * bone.matrix[12] + bone.matrix[13] * bone.matrix[13]
                                        + bone.matrix[14] * bone.matrix[14]));
    if (size <= 0) size = 1;

    int mostKeyed = 0;
    for (const P2Motion *m : motions) mostKeyed = std::max(mostKeyed, keyedBones(*m));
    QVector<int> candidates;
    QVector<Pose> starts(motions.size()), ends(motions.size());
    for (int i = 0; i < motions.size(); ++i) {
        if (motions[i]->length <= 0 || keyedBones(*motions[i]) * 2 < mostKeyed) continue;
        candidates.append(i);
        starts[i] = poseAt(*motions[i], false);
        ends[i] = poseAt(*motions[i], true);
    }

    QVector<bool> loops(motions.size(), false);
    QVector<QVector<int>> next(motions.size()), previous(motions.size());
    for (int i : candidates) {
        loops[i] = samePose(ends[i], starts[i], size);
        for (int j : candidates) {
            if (i != j && samePose(ends[i], starts[j], size)) {
                next[i].append(j);
                previous[j].append(i);
            }
        }
    }
    // The motions that clearly follow i: none when its end pose is shared.
    auto follows = [&](int i) {
        QVector<int> result;
        if (next[i].size() >= Ambiguous) return result;
        for (int j : next[i])
            if (previous[j].size() < Ambiguous) result.append(j);
        return result;
    };
    // The one motion that continues i, or -1. A start that leads into a hold and to what
    // follows the hold goes to the hold first.
    auto step = [&](int i) {
        const QVector<int> options = follows(i);
        QVector<int> holds;
        for (int j : options)
            if (loops[j]) holds.append(j);
        if (holds.size() == 1) {
            const int hold = holds.first();
            if (std::all_of(options.begin(), options.end(),
                            [&](int k) { return k == hold || next[hold].contains(k); }))
                return hold;
        }
        return options.size() == 1 ? options.first() : -1;
    };

    // Start from motions nothing leads into, then from the rest (cycles).
    QSet<int> continued;
    for (int i : candidates)
        for (int j : follows(i)) continued.insert(j);
    QVector<int> order;
    for (int i : candidates)
        if (!continued.contains(i)) order.append(i);
    for (int i : candidates)
        if (continued.contains(i)) order.append(i);

    QSet<int> used;
    for (int first : order) {
        QVector<int> chain{first};
        for (int j = step(first); j >= 0 && !chain.contains(j); j = step(j)) chain.append(j);
        const bool onlyHolds = std::all_of(chain.begin(), chain.end(), [&](int k) { return loops[k]; });
        const bool seen = std::all_of(chain.begin(), chain.end(), [&](int k) { return used.contains(k); });
        if (chain.size() < 2 || onlyHolds || seen) continue;
        chains.append(chain);
        for (int k : chain) used.insert(k);
    }
    return chains;
}

P2Motion joinMotions(const QVector<const P2Motion *> &parts) {
    P2Motion joined;
    if (parts.isEmpty()) return joined;
    joined.offset = parts.first()->offset;
    const int boneCount = parts.first()->bones.size();
    joined.bones.resize(boneCount);
    for (int b = 0; b < boneCount; ++b) {
        joined.bones[b].channels = 1 | 4 | 8;
        joined.bones[b].matrix = parts.first()->bones[b].matrix;
    }
    int start = 0;
    for (const P2Motion *part : parts) {
        for (int b = 0; b < boneCount && b < part->bones.size(); ++b) {
            const P2Motion::Bone &source = part->bones[b];
            P2Motion::Bone &target = joined.bones[b];
            // Translation keys are relative to their motion's rest translation.
            auto add = [&](P2Motion::Key key, int frame) {
                key.frame = frame;
                for (int c = 0; c < 3; ++c) key.translation[c] += source.matrix[12 + c] - target.matrix[12 + c];
                // A part's first key replaces the previous part's last one (same pose).
                if (!target.keys.isEmpty() && target.keys.last().frame == frame) target.keys.last() = key;
                else target.keys.append(key);
            };
            if (source.keys.isEmpty()) {
                add(source.rest(), start);
                if (part->length > 0) add(source.rest(), start + part->length);
            } else {
                for (int k = 0; k < source.keys.size(); ++k) add(source.full(k), start + source.keys[k].frame);
            }
        }
        start += part->length;
    }
    joined.length = start;
    return joined;
}
