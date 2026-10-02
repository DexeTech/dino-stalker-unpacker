#include "GltfWriter.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

using Mat = std::array<float, 16>;  // row vectors, translation in [12..14]
using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>;  // glTF order: x, y, z, w

// The stored row-vector matrices have the same memory layout as glTF's column-major
// column-vector matrices, so they can be used as glTF matrices directly.
Mat multiply(const Mat &a, const Mat &b) {
    Mat m{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            m[r * 4 + c] = s;
        }
    return m;
}

Mat inverse(const Mat &m) {
    // General 4x4 inverse (cofactors).
    Mat inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double det = double(m[0]) * inv[0] + double(m[1]) * inv[4] + double(m[2]) * inv[8] + double(m[3]) * inv[12];
    if (std::fabs(det) < 1e-20) {
        Mat id{};
        id[0] = id[5] = id[10] = id[15] = 1;
        return id;
    }
    for (float &v : inv) v = float(v / det);
    return inv;
}

Vec3 transformPoint(const Vec3 &v, const Mat &m) {
    return {v[0] * m[0] + v[1] * m[4] + v[2] * m[8] + m[12],
            v[0] * m[1] + v[1] * m[5] + v[2] * m[9] + m[13],
            v[0] * m[2] + v[1] * m[6] + v[2] * m[10] + m[14]};
}

Vec3 transformNormal(const Vec3 &v, const Mat &m) {
    Vec3 n{v[0] * m[0] + v[1] * m[4] + v[2] * m[8],
           v[0] * m[1] + v[1] * m[5] + v[2] * m[9],
           v[0] * m[2] + v[1] * m[6] + v[2] * m[10]};
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 1e-12f) {
        for (float &c : n) c /= l;
    } else {
        n = {0, 1, 0};
    }
    return n;
}

Quat quatFromRotation(const float r[3][3]) {
    // r[row][col] of a column-vector rotation matrix.
    Quat q;
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0) {
        const float s = std::sqrt(trace + 1.0f) * 2;
        q = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25f * s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
        q = {0.25f * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
        q = {(r[0][1] + r[1][0]) / s, 0.25f * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
        q = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25f * s, (r[1][0] - r[0][1]) / s};
    }
    const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (float &c : q) c /= l;
    return q;
}

struct Trs {
    Vec3 t{{0, 0, 0}};
    Quat r{{0, 0, 0, 1}};
    Vec3 s{{1, 1, 1}};
};

Trs decompose(const Mat &m) {
    // m is also the glTF column-major matrix: column j = m[4j..4j+2].
    Trs trs;
    trs.t = {m[12], m[13], m[14]};
    float rot[3][3];
    for (int j = 0; j < 3; ++j) {
        const float len = std::sqrt(m[4 * j] * m[4 * j] + m[4 * j + 1] * m[4 * j + 1] + m[4 * j + 2] * m[4 * j + 2]);
        trs.s[j] = len;
        for (int i = 0; i < 3; ++i) rot[i][j] = len > 1e-12f ? m[4 * j + i] / len : (i == j ? 1.0f : 0.0f);
    }
    trs.r = quatFromRotation(rot);
    return trs;
}

QJsonArray array(const float *v, int n) {
    QJsonArray a;
    for (int i = 0; i < n; ++i) a.append(double(v[i]));
    return a;
}

bool nearly(float a, float b) { return std::fabs(a - b) < 1e-5f; }

float srgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

class Builder {
public:
    QByteArray buffer;
    QJsonArray bufferViews, accessors;

    int addView(const QByteArray &bytes, int target) {
        while (buffer.size() % 4) buffer.append('\0');
        QJsonObject view{{"buffer", 0}, {"byteOffset", buffer.size()}, {"byteLength", bytes.size()}};
        if (target) view["target"] = target;
        buffer.append(bytes);
        bufferViews.append(view);
        return int(bufferViews.size() - 1);
    }

    int addFloats(const QVector<float> &values, int components, const QString &type, int target,
                  bool minMax = false) {
        QByteArray bytes(reinterpret_cast<const char *>(values.constData()), int(values.size() * sizeof(float)));
        const int view = addView(bytes, target);
        QJsonObject acc{{"bufferView", view}, {"componentType", 5126}, {"count", values.size() / components},
                        {"type", type}};
        if (minMax && !values.isEmpty()) {
            QJsonArray mn, mx;
            for (int c = 0; c < components; ++c) {
                float lo = std::numeric_limits<float>::max(), hi = -lo;
                for (int i = c; i < values.size(); i += components) {
                    lo = std::min(lo, values[i]);
                    hi = std::max(hi, values[i]);
                }
                mn.append(double(lo));
                mx.append(double(hi));
            }
            acc["min"] = mn;
            acc["max"] = mx;
        }
        accessors.append(acc);
        return int(accessors.size() - 1);
    }

    int addIndices(const QVector<quint32> &indices) {
        QByteArray bytes(reinterpret_cast<const char *>(indices.constData()), int(indices.size() * 4));
        const int view = addView(bytes, 34963);
        accessors.append(QJsonObject{{"bufferView", view}, {"componentType", 5125}, {"count", indices.size()},
                                     {"type", "SCALAR"}});
        return int(accessors.size() - 1);
    }

    int addJoints(const QVector<quint16> &joints) {
        QByteArray bytes(reinterpret_cast<const char *>(joints.constData()), int(joints.size() * 2));
        const int view = addView(bytes, 34962);
        accessors.append(QJsonObject{{"bufferView", view}, {"componentType", 5123}, {"count", joints.size() / 4},
                                     {"type", "VEC4"}});
        return int(accessors.size() - 1);
    }
};

// One primitive's worth of vertices for a material.
struct Primitive {
    QVector<float> positions, normals, uvs, colors, weights;
    QVector<quint16> joints;
    QVector<quint32> indices;
    bool hasNormals = true, hasUvs = true, hasColors = false;
};

QString alphaMode(const QImage &image, bool *hasPartial) {
    bool transparent = false, partial = false;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int a = qAlpha(line[x]);
            if (a < 255) transparent = true;
            if (a > 0 && a < 255) partial = true;
        }
    }
    *hasPartial = partial;
    if (!transparent) return "OPAQUE";
    return partial ? "BLEND" : "MASK";
}

// Characters, items and shadows are modelled in millimetres (the largest is about
// 16,600 units long), levels and debris in metres (the largest about 280). Anything
// over 500 units in its rest pose is taken to be in millimetres.
double automaticScale(const P2Model &model) {
    QVector<Mat> world(model.parts.size());
    Vec3 lo{{1e30f, 1e30f, 1e30f}}, hi{{-1e30f, -1e30f, -1e30f}};
    for (int i = 0; i < model.parts.size(); ++i) {
        const P2Model::Part &part = model.parts[i];
        world[i] = part.parent < 0 ? part.matrix : multiply(part.matrix, world[part.parent]);
        for (const auto &group : part.groups)
            for (const auto &strip : group.strips)
                for (const auto &v : strip.vertices) {
                    const Vec3 p = transformPoint(v.position, world[i]);
                    for (int c = 0; c < 3; ++c) {
                        lo[c] = std::min(lo[c], p[c]);
                        hi[c] = std::max(hi[c], p[c]);
                    }
                }
    }
    float size = 0;
    for (int c = 0; c < 3; ++c) size = std::max(size, hi[c] - lo[c]);
    return size > 500 ? 0.001 : 1.0;
}

} // namespace

bool GltfWriter::write(const QString &fileName, const QString &modelName, const P2Model &model,
                       const QVector<const QImage *> &textures, const QVector<GltfMotion> &motions,
                       const GltfOptions &options, QString *error) {
    // Bake the scale into the positions, so the model imports at its real size with a
    // scale of 1.
    const double scale = options.scale > 0 ? options.scale : automaticScale(model);
    if (scale != 1.0) {
        const float s = float(scale);
        P2Model scaledModel = model;
        for (auto &part : scaledModel.parts) {
            for (int c = 12; c < 15; ++c) part.matrix[c] *= s;
            for (auto &group : part.groups)
                for (auto &strip : group.strips)
                    for (auto &v : strip.vertices)
                        for (float &p : v.position) p *= s;
        }
        QVector<P2Motion> scaledMotions;
        scaledMotions.reserve(motions.size());
        QVector<GltfMotion> scaledRefs;
        for (const GltfMotion &gm : motions) {
            P2Motion m = *gm.motion;
            for (auto &bone : m.bones) {
                for (int c = 12; c < 15; ++c) bone.matrix[c] *= s;
                for (auto &key : bone.keys)
                    for (float &t : key.translation) t *= s;
            }
            scaledMotions.append(m);
            scaledRefs.append(GltfMotion{&scaledMotions.last(), gm.name});
        }
        GltfOptions unscaled = options;
        unscaled.scale = 1.0;
        return write(fileName, modelName, scaledModel, textures, scaledRefs, unscaled, error);
    }

    const int partCount = model.parts.size();
    bool skinnedModel = !motions.isEmpty();
    for (const auto &part : model.parts) skinnedModel = skinnedModel || part.skinned;

    // Rest pose: world matrices for every part.
    QVector<Mat> world(partCount);
    for (int i = 0; i < partCount; ++i) {
        const Mat local = model.parts[i].matrix;
        world[i] = model.parts[i].parent < 0 ? local : multiply(local, world[model.parts[i].parent]);
    }

    Builder b;
    QJsonArray meshes, materials, texturesJson, images, samplers;
    QHash<int, int> materialForTexture;   // texture index -> material
    QHash<int, int> textureForIndex;      // texture index -> glTF texture

    QVector<std::array<float, 4>> untexturedColors;
    auto materialFor = [&](int texture, const P2Model::Group &group) -> int {
        int key = texture;
        if (texture < 0) {
            // Untextured groups get one material per distinct colour.
            int found = untexturedColors.indexOf(group.diffuse);
            if (found < 0) {
                untexturedColors.append(group.diffuse);
                found = untexturedColors.size() - 1;
            }
            key = -1 - found;
        }
        if (materialForTexture.contains(key)) return materialForTexture[key];
        QJsonObject pbr{{"metallicFactor", 0.0}, {"roughnessFactor", 1.0}};
        QJsonObject material{{"name", texture >= 0 ? QString("texture_%1").arg(texture)
                                                   : QString("untextured_%1").arg(-1 - key)},
                             {"doubleSided", true}};
        const QImage *image = texture >= 0 && texture < textures.size() ? textures[texture] : nullptr;
        if (image && !image->isNull()) {
            if (!textureForIndex.contains(texture)) {
                QByteArray png;
                QBuffer io(&png);
                io.open(QIODevice::WriteOnly);
                image->save(&io, "PNG");
                images.append(QJsonObject{{"name", QString("texture_%1").arg(texture)}, {"mimeType", "image/png"},
                                          {"uri", "data:image/png;base64," + QString::fromLatin1(png.toBase64())}});
                if (samplers.isEmpty()) samplers.append(QJsonObject{{"magFilter", 9729}, {"minFilter", 9729}});
                texturesJson.append(QJsonObject{{"source", images.size() - 1}, {"sampler", 0}});
                textureForIndex[texture] = texturesJson.size() - 1;
            }
            pbr["baseColorTexture"] = QJsonObject{{"index", textureForIndex[texture]}};
            bool partial = false;
            const QString mode = alphaMode(*image, &partial);
            if (mode != "OPAQUE") material["alphaMode"] = mode;
        } else {
            const float *d = group.diffuse.data();
            pbr["baseColorFactor"] = QJsonArray{std::clamp(double(d[0]), 0.0, 1.0), std::clamp(double(d[1]), 0.0, 1.0),
                                                std::clamp(double(d[2]), 0.0, 1.0), 1.0};
        }
        material["pbrMetallicRoughness"] = pbr;
        materials.append(material);
        materialForTexture[key] = materials.size() - 1;
        return int(materials.size() - 1);
    };

    // Collects a part's (or the whole model's) triangles into primitives by material.
    auto addGroup = [&](QMap<int, Primitive> &prims, const P2Model::Group &group, int partIndex, bool toBindSpace) {
        const int material = materialFor(group.texture, group);
        Primitive &prim = prims[material];
        const bool hasNormals = group.attributes & 1, hasColors = group.attributes & 2, hasUvs = group.attributes & 4;
        if (prim.positions.isEmpty()) {
            prim.hasNormals = hasNormals;
            prim.hasUvs = hasUvs;
            prim.hasColors = hasColors;
        } else {
            // Groups sharing a material may differ; missing attributes get defaults.
            prim.hasNormals = prim.hasNormals || hasNormals;
            prim.hasUvs = prim.hasUvs || hasUvs;
            prim.hasColors = prim.hasColors || hasColors;
        }
        for (const auto &strip : group.strips) {
            const int base = prim.positions.size() / 3;
            for (const auto &v : strip.vertices) {
                const Mat &m = toBindSpace ? world[v.bone] : world[partIndex];
                Vec3 p = toBindSpace ? transformPoint(v.position, m) : v.position;
                Vec3 n = toBindSpace ? transformNormal(v.normal, m) : v.normal;
                if (toBindSpace == false && v.bone != partIndex) {
                    // A static part whose vertex is positioned by another part.
                    const Mat rel = multiply(world[v.bone], inverse(world[partIndex]));
                    p = transformPoint(v.position, rel);
                    n = transformNormal(v.normal, rel);
                }
                prim.positions << p[0] << p[1] << p[2];
                if (!hasNormals) n = {0, 1, 0};
                prim.normals << n[0] << n[1] << n[2];
                prim.uvs << v.uv[0] << v.uv[1];
                // The GS multiplies texture and vertex colour in display (sRGB) space;
                // glTF multiplies in linear space, so the colour is converted.
                prim.colors << srgbToLinear(v.color[0]) << srgbToLinear(v.color[1]) << srgbToLinear(v.color[2])
                            << v.color[3];
                prim.joints << quint16(v.bone) << 0 << 0 << 0;
                prim.weights << 1.0f << 0.0f << 0.0f << 0.0f;
            }
            for (int k = 2; k < strip.vertices.size(); ++k) {
                if (!strip.vertices[k].drawTriangle) continue;
                const quint32 a = base + k - 2, c = base + k - 1, d = base + k;
                if (k % 2 == 0) prim.indices << a << c << d;
                else prim.indices << c << a << d;
            }
        }
    };

    auto addMesh = [&](QMap<int, Primitive> &prims, const QString &name, bool skinned) -> int {
        QJsonArray primitives;
        for (auto it = prims.begin(); it != prims.end(); ++it) {
            Primitive &p = it.value();
            if (p.indices.isEmpty()) continue;
            QJsonObject attributes{{"POSITION", b.addFloats(p.positions, 3, "VEC3", 34962, true)}};
            if (p.hasNormals) attributes["NORMAL"] = b.addFloats(p.normals, 3, "VEC3", 34962);
            if (p.hasUvs) attributes["TEXCOORD_0"] = b.addFloats(p.uvs, 2, "VEC2", 34962);
            if (p.hasColors) attributes["COLOR_0"] = b.addFloats(p.colors, 4, "VEC4", 34962);
            if (skinned) {
                attributes["JOINTS_0"] = b.addJoints(p.joints);
                attributes["WEIGHTS_0"] = b.addFloats(p.weights, 4, "VEC4", 34962);
            }
            primitives.append(QJsonObject{{"attributes", attributes}, {"indices", b.addIndices(p.indices)},
                                          {"material", it.key()}, {"mode", 4}});
        }
        if (primitives.isEmpty()) return -1;
        meshes.append(QJsonObject{{"name", name}, {"primitives", primitives}});
        return int(meshes.size() - 1);
    };

    // Part nodes, with TRS so they can be animated.
    const int firstPart = 1;  // node 0 is the axis-conversion root
    QVector<QJsonArray> children(partCount);
    QJsonArray rootChildren;
    for (int i = 0; i < partCount; ++i) {
        if (model.parts[i].parent < 0) rootChildren.append(firstPart + i);
        else children[model.parts[i].parent].append(firstPart + i);
    }
    QVector<QJsonObject> partNodes(partCount);
    for (int i = 0; i < partCount; ++i) {
        const Trs trs = decompose(model.parts[i].matrix);
        QJsonObject node{{"name", QString("part_%1").arg(i, 2, 10, QChar('0'))}};
        if (!(nearly(trs.t[0], 0) && nearly(trs.t[1], 0) && nearly(trs.t[2], 0))) node["translation"] = array(trs.t.data(), 3);
        if (!(nearly(trs.r[0], 0) && nearly(trs.r[1], 0) && nearly(trs.r[2], 0))) node["rotation"] = array(trs.r.data(), 4);
        if (!(nearly(trs.s[0], 1) && nearly(trs.s[1], 1) && nearly(trs.s[2], 1))) node["scale"] = array(trs.s.data(), 3);
        if (!children[i].isEmpty()) node["children"] = children[i];
        partNodes[i] = node;
    }

    QJsonArray skins;
    int skinnedMeshNode = -1;
    QJsonObject meshNode;
    if (skinnedModel) {
        QMap<int, Primitive> prims;
        for (int i = 0; i < partCount; ++i)
            for (const auto &group : model.parts[i].groups) addGroup(prims, group, i, true);
        const int mesh = addMesh(prims, modelName, true);
        QVector<float> ibm;
        QJsonArray joints;
        for (int i = 0; i < partCount; ++i) {
            Mat inv = inverse(world[i]);
            // glTF requires an exact affine last row.
            inv[3] = inv[7] = inv[11] = 0.0f;
            inv[15] = 1.0f;
            for (float v : inv) ibm << v;
            joints.append(firstPart + i);
        }
        const int ibmAccessor = b.addFloats(ibm, 16, "MAT4", 0);
        skins.append(QJsonObject{{"name", modelName}, {"joints", joints}, {"inverseBindMatrices", ibmAccessor},
                                 {"skeleton", firstPart + 0}});
        if (mesh >= 0) {
            // A skinned mesh node must be a scene root; the joints carry the axis turn.
            skinnedMeshNode = firstPart + partCount;
            meshNode = QJsonObject{{"name", modelName}, {"mesh", mesh}, {"skin", 0}};
        }
    } else {
        for (int i = 0; i < partCount; ++i) {
            QMap<int, Primitive> prims;
            for (const auto &group : model.parts[i].groups) addGroup(prims, group, i, false);
            const int mesh = addMesh(prims, QString("%1_part_%2").arg(modelName).arg(i), false);
            if (mesh >= 0) partNodes[i]["mesh"] = mesh;
        }
    }

    // Root: -Y up to +Y up is a 180 degree turn about X.
    QJsonObject root{{"name", modelName}, {"rotation", QJsonArray{1.0, 0.0, 0.0, 0.0}}, {"children", rootChildren}};
    QJsonArray allNodes;
    allNodes.append(root);
    for (int i = 0; i < partCount; ++i) allNodes.append(partNodes[i]);
    if (skinnedMeshNode >= 0) allNodes.append(meshNode);

    // Animations.
    QJsonArray animations;
    for (const GltfMotion &gm : motions) {
        const P2Motion &motion = *gm.motion;
        if (motion.bones.size() != partCount) continue;
        QJsonArray channels, samplersJson;
        auto addSampler = [&](const QVector<float> &times, const QVector<float> &values, int components,
                              const QString &type, int node, const QString &path) {
            const int input = b.addFloats(times, 1, "SCALAR", 0, true);
            const int output = b.addFloats(values, components, type, 0);
            samplersJson.append(QJsonObject{{"input", input}, {"output", output}, {"interpolation", "LINEAR"}});
            channels.append(QJsonObject{{"sampler", samplersJson.size() - 1},
                                        {"target", QJsonObject{{"node", node}, {"path", path}}}});
        };
        for (int bone = 0; bone < partCount; ++bone) {
            const P2Motion::Bone &mb = motion.bones[bone];
            if (mb.keys.isEmpty()) continue;
            QVector<float> times;
            for (const auto &key : mb.keys) times << float(key.frame / options.framesPerSecond);
            if (mb.channels & 1) {
                QVector<float> values;
                Quat previous{{0, 0, 0, 1}};
                for (const auto &key : mb.keys) {
                    // Stored w, x, y, z; the game applies it to row vectors, so the
                    // glTF rotation is its conjugate.
                    Quat q{-key.rotation[1], -key.rotation[2], -key.rotation[3], key.rotation[0]};
                    const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                    if (len > 1e-6f) for (float &c : q) c /= len;
                    else q = {0, 0, 0, 1};
                    if (q[0] * previous[0] + q[1] * previous[1] + q[2] * previous[2] + q[3] * previous[3] < 0)
                        for (float &c : q) c = -c;
                    previous = q;
                    values << q[0] << q[1] << q[2] << q[3];
                }
                addSampler(times, values, 4, "VEC4", firstPart + bone, "rotation");
            }
            if (mb.channels & 4) {
                QVector<float> values;
                for (const auto &key : mb.keys)
                    for (int c = 0; c < 3; ++c) values << mb.matrix[12 + c] + key.translation[c];
                addSampler(times, values, 3, "VEC3", firstPart + bone, "translation");
            }
            if (mb.channels & 8) {
                QVector<float> values;
                for (const auto &key : mb.keys)
                    for (int c = 0; c < 3; ++c) values << key.scale[c];
                addSampler(times, values, 3, "VEC3", firstPart + bone, "scale");
            }
        }
        if (!channels.isEmpty())
            animations.append(QJsonObject{{"name", gm.name}, {"channels", channels}, {"samplers", samplersJson}});
    }

    QJsonObject gltf{{"asset", QJsonObject{{"version", "2.0"}, {"generator", "dino-stalker-unpacker"}}},
                     {"scene", 0},
                     {"scenes", QJsonArray{QJsonObject{{"name", modelName},
                                                       {"nodes", skinnedMeshNode >= 0 ? QJsonArray{0, skinnedMeshNode}
                                                                                     : QJsonArray{0}}}}},
                     {"nodes", allNodes}};
    if (!meshes.isEmpty()) gltf["meshes"] = meshes;
    if (!materials.isEmpty()) gltf["materials"] = materials;
    if (!texturesJson.isEmpty()) {
        gltf["textures"] = texturesJson;
        gltf["images"] = images;
        gltf["samplers"] = samplers;
    }
    if (!skins.isEmpty() && skinnedMeshNode >= 0) gltf["skins"] = skins;
    if (!animations.isEmpty()) gltf["animations"] = animations;
    if (!b.buffer.isEmpty()) {
        gltf["buffers"] = QJsonArray{QJsonObject{
            {"byteLength", b.buffer.size()},
            {"uri", "data:application/octet-stream;base64," + QString::fromLatin1(b.buffer.toBase64())}}};
        gltf["bufferViews"] = b.bufferViews;
        gltf["accessors"] = b.accessors;
    }

    QFile out(fileName);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = out.errorString();
        return false;
    }
    out.write(QJsonDocument(gltf).toJson(QJsonDocument::Compact));
    return true;
}
