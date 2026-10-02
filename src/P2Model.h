#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <array>

// A "P2OD" model block: a tree of parts, each with an optional mesh.
//
//   0x00 "P2OD", u32 0x80, u32 part count, padding to 0x20
//   0x20 part records, 0x50 bytes each:
//        s16 parent, s16 first child, s16 next sibling, u16 0,
//        u32 mesh offset (from the block start, 0 = no mesh), u32 0,
//        float matrix[16]  (row vectors: translation in the last row)
//
// Mesh: u32 group count, u32 flags (bit 0 = skinned), 8 bytes,
//       float bounds min[4], max[4], then the groups.
// Group (0x60 bytes): u32 strip count, u32 attributes, s32 texture (-1 = none), u32,
//       float[4] (first = 50.0), four RGBA float colours (0-255 scale), then strips.
// Attributes: bit 0 normals, bit 1 vertex colours, bit 2 texture coordinates.
// Strip: u32 vertex count, u32 GS primitive bits, 8 bytes, then per-vertex arrays
//       of 16-byte entries in this order: position, normal, colour, UV.
//       Position w: bit 15 = draw the triangle ending here; for skinned meshes the
//       low 15 bits are the part whose matrix positions the vertex (the vertex's
//       own part, or its parent at seams). Vertices are in that part's space.
//       UV: s, t, q (1.0), 0.  Colour: r, g, b, a floats on a 0-255 scale.
struct P2Model {
    struct Vertex {
        std::array<float, 3> position;
        std::array<float, 3> normal{{0, 0, 0}};
        std::array<float, 4> color{{1, 1, 1, 1}};
        std::array<float, 2> uv{{0, 0}};
        int bone = 0;
        bool drawTriangle = false;
    };
    struct Strip {
        QVector<Vertex> vertices;
    };
    struct Group {
        int texture = -1;
        quint32 attributes = 0;
        std::array<float, 4> diffuse{{1, 1, 1, 1}};
        QVector<Strip> strips;
    };
    struct Part {
        int parent = -1;
        std::array<float, 16> matrix{};
        bool skinned = false;
        QVector<Group> groups;
    };

    qint64 offset = 0;
    qint64 size = 0;
    QVector<Part> parts;

    static bool parse(const QByteArray &data, qint64 offset, P2Model &model, QString *error = nullptr);
    bool hasGeometry() const;
    int maxTextureIndex() const;
};
