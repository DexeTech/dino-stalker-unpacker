#include "P2Model.h"
#include "Binary.h"

#include <algorithm>

namespace {
const quint32 MAX_PARTS = 512;
const quint32 MAX_GROUPS = 4096;
const quint32 MAX_STRIP = 4096;
}

bool P2Model::parse(const QByteArray &data, qint64 offset, P2Model &model, QString *error) {
    Reader r(data);
    auto fail = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (!r.matches(offset, "P2OD")) {
        return fail("Not a P2OD block.");
    }
    const quint32 count = r.u32(offset + 8);
    if (count == 0 || count > MAX_PARTS || !r.has(offset + 0x20, qint64(count) * 0x50)) {
        return fail("Part table is out of range.");
    }
    model.offset = offset;
    model.parts.clear();
    qint64 end = offset + 0x20 + qint64(count) * 0x50;

    for (quint32 i = 0; i < count; ++i) {
        const qint64 rec = offset + 0x20 + qint64(i) * 0x50;
        Part part;
        part.parent = r.s16(rec);
        // Parents always come before their children.
        if (part.parent < -1 || part.parent >= int(i)) {
            return fail(QString("Part %1 has an invalid parent.").arg(i));
        }
        for (int k = 0; k < 16; ++k) {
            part.matrix[k] = r.f32(rec + 0x10 + k * 4);
        }
        const quint32 meshOffset = r.u32(rec + 8);
        if (meshOffset) {
            qint64 p = offset + meshOffset;
            const quint32 groups = r.u32(p);
            const quint32 flags = r.u32(p + 4);
            if (groups > MAX_GROUPS || !r.has(p, 0x30)) {
                return fail(QString("Part %1 has an invalid mesh.").arg(i));
            }
            part.skinned = flags & 1;
            p += 0x30;
            for (quint32 g = 0; g < groups; ++g) {
                Group group;
                const quint32 strips = r.u32(p);
                group.attributes = r.u32(p + 4);
                group.texture = r.s32(p + 8);
                for (int c = 0; c < 4; ++c) {
                    group.diffuse[c] = r.f32(p + 0x20 + c * 4) / 255.0f;
                }
                if ((group.attributes & ~0x18107u) || strips > MAX_STRIP || !r.has(p, 0x60)) {
                    return fail(QString("Part %1 has an invalid material group.").arg(i));
                }
                p += 0x60;
                const bool hasNormal = group.attributes & 1;
                const bool hasColor = group.attributes & 2;
                const bool hasUv = group.attributes & 4;
                const int arrays = 1 + hasNormal + hasColor + hasUv;
                for (quint32 s = 0; s < strips; ++s) {
                    const quint32 n = r.u32(p);
                    if (n > MAX_STRIP || !r.has(p, 0x10 + qint64(n) * 0x10 * arrays)) {
                        return fail(QString("Part %1 has an invalid strip.").arg(i));
                    }
                    p += 0x10;
                    Strip strip;
                    strip.vertices.resize(int(n));
                    qint64 a = p;
                    for (quint32 v = 0; v < n; ++v) {
                        Vertex &vx = strip.vertices[int(v)];
                        const qint64 q = a + v * 0x10;
                        vx.position = {r.f32(q), r.f32(q + 4), r.f32(q + 8)};
                        const quint32 w = r.u32(q + 12);
                        vx.drawTriangle = w & 0x8000;
                        vx.bone = part.skinned ? int(w & 0x7FFF) : int(i);
                        if (vx.bone >= int(count)) {
                            return fail(QString("Part %1 has a vertex bound to a missing part.").arg(i));
                        }
                    }
                    a += qint64(n) * 0x10;
                    if (hasNormal) {
                        for (quint32 v = 0; v < n; ++v) {
                            const qint64 q = a + v * 0x10;
                            strip.vertices[int(v)].normal = {r.f32(q), r.f32(q + 4), r.f32(q + 8)};
                        }
                        a += qint64(n) * 0x10;
                    }
                    if (hasColor) {
                        for (quint32 v = 0; v < n; ++v) {
                            const qint64 q = a + v * 0x10;
                            auto &c = strip.vertices[int(v)].color;
                            for (int k = 0; k < 4; ++k) {
                                c[k] = std::clamp(r.f32(q + k * 4) / 128.0f, 0.0f, 1.0f);
                            }
                        }
                        a += qint64(n) * 0x10;
                    }
                    if (hasUv) {
                        for (quint32 v = 0; v < n; ++v) {
                            const qint64 q = a + v * 0x10;
                            strip.vertices[int(v)].uv = {r.f32(q), r.f32(q + 4)};
                        }
                        a += qint64(n) * 0x10;
                    }
                    p = a;
                    group.strips.append(strip);
                }
                part.groups.append(group);
            }
            end = std::max(end, p);
        }
        model.parts.append(part);
    }
    if (!r.ok()) {
        return fail("Model data is truncated.");
    }
    model.size = end - offset;
    return true;
}

bool P2Model::hasGeometry() const {
    for (const Part &part : parts) {
        for (const Group &group : part.groups) {
            for (const Strip &strip : group.strips) {
                if (strip.vertices.size() >= 3) return true;
            }
        }
    }
    return false;
}

int P2Model::maxTextureIndex() const {
    int result = -1;
    for (const Part &part : parts) {
        for (const Group &group : part.groups) {
            result = std::max(result, group.texture);
        }
    }
    return result;
}
